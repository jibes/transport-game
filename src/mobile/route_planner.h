/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file route_planner.h Rail route planning between two tiles (touch "Route" tool). */

#ifndef MOBILE_ROUTE_PLANNER_H
#define MOBILE_ROUTE_PLANNER_H

#include "../tile_type.h"
#include "../track_type.h"
#include "../rail_type.h"
#include "../economy_type.h"

#include <vector>

namespace Touch {

/** Trade-off a route is optimised for; the player picks between the resulting alternatives. */
enum class RouteStyle : uint8_t { Cheap, Balanced, Direct, End };

/** One track piece of a route. */
struct RouteStep {
	TileIndex tile;
	Track track;
	bool exists; ///< Already built (own track): reused, costs nothing.
};

struct RoutePlan {
	std::vector<RouteStep> steps;
	Money cost = 0;     ///< Construction cost of the new pieces (game's own estimate).
	int curves = 0;     ///< Changes of direction.
	int slopes = 0;     ///< Pieces on sloped tiles.
	bool found = false;
};

RoutePlan PlanRailRoute(TileIndex from, TileIndex to, RailType railtype, RouteStyle style);

} // namespace Touch

#endif /* MOBILE_ROUTE_PLANNER_H */
