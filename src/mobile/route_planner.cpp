/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file route_planner.cpp A* rail route planning over track pieces.
 *
 * Search state: (tile, trackdir). Successors: the trackdirs the next tile is entered with, without
 * 90 degree turns. Whether a piece can be built, and what it costs, is asked from the game itself
 * (BuildRail command in test mode), so slopes, foundations, obstacles and ownership follow the
 * game rules exactly. Own existing track is reused for free. Nothing is built here.
 *
 * Styles weight money against length, curves and slopes; they produce the alternatives the
 * player chooses from - the planner never decides which one is "best".
 */

#include "../stdafx.h"
#include "route_planner.h"
#include "../command_func.h"
#include "../rail_cmd.h"
#include "../rail_map.h"
#include "../station_map.h"
#include "../track_func.h"
#include "../tile_map.h"
#include "../map_func.h"
#include "../company_func.h"
#include "../economy_func.h"
#include "../slope_func.h"

#include <queue>
#include <unordered_map>

#include "../safeguards.h"

namespace Touch {

/** Weights per style, in units of "one flat rail piece". */
struct StyleWeights {
	int money_percent; ///< How much the construction cost counts.
	int per_tile;      ///< Cost of length.
	int per_curve;     ///< Cost of a change of direction.
	int per_slope;     ///< Cost of a piece on a slope (beyond its money cost).
};

static constexpr StyleWeights STYLE_WEIGHTS[] = {
	/* Cheap    */ {100, 0, 0, 0},
	/* Balanced */ {100, 1, 2, 1},
	/* Direct   */ {25, 2, 8, 0},
};

static constexpr int MAX_EXPANSIONS = 200000;
static constexpr int SEARCH_MARGIN = 24; ///< Tiles around the A-B box the route may use.

/** 8-way heading of a trackdir; a change of heading is a curve. */
static int Heading(Trackdir td)
{
	switch (td) {
		case Trackdir::X_NE: return 0;
		case Trackdir::Upper_E: case Trackdir::Lower_E: return 1;
		case Trackdir::Y_SE: return 2;
		case Trackdir::Left_S: case Trackdir::Right_S: return 3;
		case Trackdir::X_SW: return 4;
		case Trackdir::Upper_W: case Trackdir::Lower_W: return 5;
		case Trackdir::Y_NW: return 6;
		case Trackdir::Left_N: case Trackdir::Right_N: return 7;
		default: return -1;
	}
}

static uint32_t Key(TileIndex tile, Trackdir td)
{
	return (tile.base() << 4) | to_underlying(td);
}

RoutePlan PlanRailRoute(TileIndex from, TileIndex to, RailType railtype, RouteStyle style)
{
	RoutePlan plan;
	if (!IsValidTile(from) || !IsValidTile(to) || from == to) return plan;
	const StyleWeights &w = STYLE_WEIGHTS[to_underlying(style)];

	/* One flat piece of rail is the unit of all weights. */
	const Money unit = std::max<Money>(1, _price[Price::BuildRail]);

	const uint min_x = std::min(TileX(from), TileX(to)) > SEARCH_MARGIN ? std::min(TileX(from), TileX(to)) - SEARCH_MARGIN : 0;
	const uint min_y = std::min(TileY(from), TileY(to)) > SEARCH_MARGIN ? std::min(TileY(from), TileY(to)) - SEARCH_MARGIN : 0;
	const uint max_x = std::max(TileX(from), TileX(to)) + SEARCH_MARGIN;
	const uint max_y = std::max(TileY(from), TileY(to)) + SEARCH_MARGIN;
	auto in_box = [&](TileIndex t) { return TileX(t) >= min_x && TileX(t) <= max_x && TileY(t) >= min_y && TileY(t) <= max_y; };

	/* Cost of one piece: -1 if it cannot be built, 0 if our own track is already there. Cached. */
	std::unordered_map<uint32_t, Money> piece_cache;
	auto piece_cost = [&](TileIndex t, Track track) -> Money {
		uint32_t k = (t.base() << 3) | to_underlying(track);
		auto it = piece_cache.find(k);
		if (it != piece_cache.end()) return it->second;
		Money c;
		if (IsPlainRailTile(t) && GetTileOwner(t) == _local_company && HasTrack(t, track)) {
			c = 0;
		} else if (IsRailStationTile(t) || IsRailDepotTile(t)) {
			/* Own station platforms and depot entries are track along their axis; never build there. */
			bool own = GetTileOwner(t) == _local_company;
			Track along = IsRailStationTile(t) ? GetRailStationTrack(t) : GetRailDepotTrack(t);
			c = (own && along == track) ? Money(0) : Money(-1);
		} else {
			CommandCost res = Command<Commands::BuildRail>::Do(DoCommandFlags{}, t, railtype, track, false);
			c = res.Succeeded() ? std::max<Money>(res.GetCost(), 1) : Money(-1);
		}
		piece_cache[k] = c;
		return c;
	};

	/* Weighted step cost (internal units: 1/100 of a flat piece). */
	auto step_cost = [&](Money money, bool curve, bool slope) -> int64_t {
		return money * w.money_percent / unit + 100 * (w.per_tile + (curve ? w.per_curve : 0) + (slope ? w.per_slope : 0));
	};
	/* Admissible: every further tile costs at least its length weight (and money for new pieces in Cheap). */
	const int64_t min_step = std::max<int64_t>(1, 100 * w.per_tile);
	auto heuristic = [&](TileIndex t) -> int64_t { return static_cast<int64_t>(DistanceManhattan(t, to)) * min_step; };

	struct Node { int64_t f; uint32_t key; bool operator>(const Node &o) const { return f > o.f; } };
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
	std::unordered_map<uint32_t, int64_t> g;
	std::unordered_map<uint32_t, uint32_t> parent;
	static constexpr uint32_t NO_PARENT = UINT32_MAX;

	for (Trackdir td : {Trackdir::X_NE, Trackdir::Y_SE, Trackdir::Upper_E, Trackdir::Lower_E, Trackdir::Left_S, Trackdir::Right_S,
			Trackdir::X_SW, Trackdir::Y_NW, Trackdir::Upper_W, Trackdir::Lower_W, Trackdir::Left_N, Trackdir::Right_N}) {
		Money c = piece_cost(from, TrackdirToTrack(td));
		if (c < 0) continue;
		uint32_t k = Key(from, td);
		int64_t gc = step_cost(c, false, GetTileSlope(from) != SLOPE_FLAT);
		g[k] = gc;
		parent[k] = NO_PARENT;
		open.push({gc + heuristic(from), k});
	}

	uint32_t goal = NO_PARENT;
	for (int expansions = 0; !open.empty() && expansions < MAX_EXPANSIONS; expansions++) {
		Node n = open.top();
		open.pop();
		TileIndex t{n.key >> 4};
		Trackdir td = static_cast<Trackdir>(n.key & 0xF);
		int64_t gn = g[n.key];
		if (n.f > gn + heuristic(t)) continue; // stale entry
		if (t == to) {
			goal = n.key;
			break;
		}

		TileIndex nt = TileAddByDiagDir(t, TrackdirToExitdir(td));
		if (!IsValidTile(nt) || !in_box(nt)) continue;
		TrackdirBits next = TrackdirReachesTrackdirs(td);
		next.Reset(TrackdirCrossesTrackdirs(td)); // no 90 degree turns
		for (Trackdir td2 : next) {
			Money c = piece_cost(nt, TrackdirToTrack(td2));
			if (c < 0) continue;
			uint32_t k2 = Key(nt, td2);
			int64_t g2 = gn + step_cost(c, Heading(td2) != Heading(td), GetTileSlope(nt) != SLOPE_FLAT);
			auto it = g.find(k2);
			if (it != g.end() && it->second <= g2) continue;
			g[k2] = g2;
			parent[k2] = n.key;
			open.push({g2 + heuristic(nt), k2});
		}
	}
	if (goal == NO_PARENT) return plan;

	/* Walk back from the goal; the result runs from A to B. */
	int prev_heading = -1;
	for (uint32_t k = goal; k != NO_PARENT; k = parent[k]) {
		TileIndex t{k >> 4};
		Trackdir td = static_cast<Trackdir>(k & 0xF);
		Track track = TrackdirToTrack(td);
		Money c = piece_cost(t, track);
		plan.steps.push_back({t, track, c == 0});
		plan.cost += std::max<Money>(c, 0);
		if (GetTileSlope(t) != SLOPE_FLAT && c > 0) plan.slopes++;
		if (prev_heading >= 0 && Heading(td) != prev_heading) plan.curves++;
		prev_heading = Heading(td);
	}
	std::reverse(plan.steps.begin(), plan.steps.end());
	plan.found = true;
	return plan;
}

} // namespace Touch
