/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file card_gui.cpp Tap-to-select cards (touch).
 *
 * Tapping a thing on the map shows a card: a few lines of live information (small) and its most
 * used actions (finger-sized), plus "More" for the full game window. One card at a time; it
 * opens as a sheet above the bottom row. Actions are the same functions/commands as the game windows.
 */

#include "../stdafx.h"
#include "card_gui.h"
#include "touch_input.h"
#include "mobile_bar_gui.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../viewport_func.h"
#include "../vehicle_base.h"
#include "../vehicle_func.h"
#include "../vehicle_gui.h"
#include "../command_func.h"
#include "../vehicle_cmd.h"
#include "../station_base.h"
#include "../station_func.h"
#include "../station_map.h"
#include "../depot_map.h"
#include "../depot_func.h"
#include "../town.h"
#include "../town_map.h"
#include "../industry.h"
#include "../industry_map.h"
#include "../tile_map.h"
#include "../map_func.h"
#include "../company_func.h"
#include "../strings_func.h"
#include "../gui.h"
#include "../gfx_func.h"
#include "../zoom_func.h"
#include "../misc/history_type.hpp"

#include "table/strings.h"

#include "../safeguards.h"

void ShowIndustryViewWindow(IndustryID industry);

namespace {

enum CardWidgets : WidgetID {
	WID_CARD_CAPTION,
	WID_CARD_INFO,
	WID_CARD_ACTION_FIRST,
};

enum class CardKind : uint8_t { Vehicle, Station, Depot, Town, Industry };
enum class CardAction : uint8_t { StartStop, ToDepot, Follow, Orders, Centre, Buy, Vehicles, More };

/** What the current card shows. Kept outside the window so the widget tree can be built from it. */
struct CardTarget {
	CardKind kind = CardKind::Vehicle;
	VehicleID vehicle = VehicleID::Invalid();
	StationID station = StationID::Invalid();
	TownID town = TownID::Invalid();
	IndustryID industry = IndustryID::Invalid();
	TileIndex tile = INVALID_TILE;
};

static CardTarget _card;
static constexpr int MAX_INFO_LINES = 3;

static std::vector<CardAction> ActionsFor(const CardTarget &t)
{
	switch (t.kind) {
		case CardKind::Vehicle: {
			const Vehicle *v = Vehicle::GetIfValid(t.vehicle);
			if (v != nullptr && v->owner == _local_company) {
				return {CardAction::StartStop, CardAction::ToDepot, CardAction::Orders, CardAction::Follow, CardAction::More};
			}
			return {CardAction::Follow, CardAction::More};
		}
		case CardKind::Station: return {CardAction::Vehicles, CardAction::Centre, CardAction::More};
		case CardKind::Depot: return {CardAction::Buy, CardAction::Vehicles, CardAction::More};
		case CardKind::Town: return {CardAction::Centre, CardAction::More};
		case CardKind::Industry: return {CardAction::Centre, CardAction::More};
	}
	NOT_REACHED();
}

static StringID ActionLabel(CardAction a)
{
	switch (a) {
		case CardAction::StartStop: return STR_MOBILE_CARD_START_STOP;
		case CardAction::ToDepot: return STR_MOBILE_CARD_TO_DEPOT;
		case CardAction::Follow: return STR_MOBILE_CARD_FOLLOW;
		case CardAction::Orders: return STR_MOBILE_CARD_ORDERS;
		case CardAction::Centre: return STR_MOBILE_CONTEXT_CENTRE;
		case CardAction::Buy: return STR_MOBILE_CARD_BUY;
		case CardAction::Vehicles: return STR_MOBILE_BAR_VEHICLES;
		case CardAction::More: return STR_MOBILE_CARD_MORE;
	}
	NOT_REACHED();
}

static VehicleType DepotType(TileIndex tile)
{
	return GetDepotVehicleType(tile);
}

/** Title of the card, or empty if the target no longer exists. */
static std::string CardTitle(const CardTarget &t)
{
	switch (t.kind) {
		case CardKind::Vehicle: return Vehicle::IsValidID(t.vehicle) ? GetString(STR_VEHICLE_NAME, t.vehicle) : std::string{};
		case CardKind::Station: return Station::IsValidID(t.station) ? GetString(STR_STATION_NAME, t.station) : std::string{};
		case CardKind::Depot: return IsValidTile(t.tile) && IsDepotTile(t.tile) ? GetString(STR_MOBILE_CARD_DEPOT) : std::string{};
		case CardKind::Town: return Town::IsValidID(t.town) ? GetString(STR_TOWN_NAME, t.town) : std::string{};
		case CardKind::Industry: return Industry::IsValidID(t.industry) ? GetString(STR_INDUSTRY_NAME, t.industry) : std::string{};
	}
	NOT_REACHED();
}

/** Live information, at most MAX_INFO_LINES lines. */
static std::vector<std::string> CardInfo(const CardTarget &t)
{
	std::vector<std::string> lines;
	switch (t.kind) {
		case CardKind::Vehicle: {
			const Vehicle *v = Vehicle::GetIfValid(t.vehicle);
			if (v == nullptr) break;
			if (v->vehstatus.Test(VehState::Stopped)) {
				lines.push_back(GetString(STR_MOBILE_CARD_STOPPED));
			} else {
				lines.push_back(GetString(STR_MOBILE_CARD_RUNNING, PackVelocity(v->GetDisplaySpeed(), v->type)));
			}
			lines.push_back(GetString(STR_MOBILE_CARD_PROFIT, v->GetDisplayProfitThisYear(), v->GetDisplayProfitLastYear()));
			break;
		}
		case CardKind::Station: {
			const Station *st = Station::GetIfValid(t.station);
			if (st == nullptr) break;
			/* Largest waiting amounts first. */
			std::vector<std::pair<uint, CargoType>> waiting;
			for (CargoType c : EnumRange(NUM_CARGO)) {
				const GoodsEntry &ge = st->goods[c];
				if (!ge.HasData()) continue;
				uint n = ge.GetData().cargo.TotalCount();
				if (n > 0) waiting.emplace_back(n, c);
			}
			std::sort(waiting.begin(), waiting.end(), [](auto &a, auto &b) { return a.first > b.first; });
			for (const auto &[n, c] : waiting) {
				if (static_cast<int>(lines.size()) == MAX_INFO_LINES) break;
				lines.push_back(GetString(STR_MOBILE_CARD_WAITING, c, n));
			}
			if (lines.empty()) lines.push_back(GetString(STR_MOBILE_CARD_NOTHING_WAITING));
			break;
		}
		case CardKind::Depot: {
			if (!IsValidTile(t.tile) || !IsDepotTile(t.tile)) break;
			uint count = 0;
			for (const Vehicle *v : Vehicle::Iterate()) {
				if (v->IsPrimaryVehicle() && v->tile == t.tile && v->IsChainInDepot()) count++;
			}
			lines.push_back(GetString(STR_MOBILE_CARD_IN_DEPOT, count));
			break;
		}
		case CardKind::Town: {
			const Town *town = Town::GetIfValid(t.town);
			if (town == nullptr) break;
			lines.push_back(GetString(STR_MOBILE_CARD_POPULATION, town->cache.population));
			break;
		}
		case CardKind::Industry: {
			const Industry *ind = Industry::GetIfValid(t.industry);
			if (ind == nullptr) break;
			for (const auto &p : ind->produced) {
				if (!IsValidCargoType(p.cargo)) continue;
				if (static_cast<int>(lines.size()) == MAX_INFO_LINES) break;
				const auto &last = p.history[LAST_MONTH];
				lines.push_back(GetString(STR_MOBILE_CARD_PRODUCED, p.cargo, last.production, ToPercent8(last.PctTransported())));
			}
			if (lines.empty()) lines.push_back(GetString(STR_MOBILE_CARD_PRODUCES_NOTHING));
			break;
		}
	}
	return lines;
}

static std::unique_ptr<NWidgetBase> MakeActionRow()
{
	auto row = std::make_unique<NWidgetHorizontal>(NWidContainerFlag::EqualSize);
	std::vector<CardAction> actions = ActionsFor(_card);
	for (size_t i = 0; i < actions.size(); i++) {
		auto b = std::make_unique<NWidgetLeaf>(WWT_PUSHTXTBTN, Colours::Grey, WID_CARD_ACTION_FIRST + static_cast<int>(i), WidgetData{.string = ActionLabel(actions[i])}, STR_NULL);
		b->SetFill(1, 1);
		b->SetResize(1, 0);
		row->Add(std::move(b));
	}
	return row;
}

struct CardWindow : Window {
	CardTarget target;
	std::vector<CardAction> actions;
	uint refresh_ms = 0;

	CardWindow(WindowDesc &desc) : Window(desc), target(_card), actions(ActionsFor(_card))
	{
		this->InitNested(0);
		/* Full safe width, like the other sheets: buttons get as much room as possible. */
		const Touch::SafeInsets &safe = Touch::GetSafeInsets();
		int area_width = _screen.width - safe.left - safe.right;
		if (this->width < area_width) ResizeWindow(this, area_width - this->width, 0, false);
		Point pt = GetSheetPosition(this->width, this->height);
		this->SetDirty();
		this->left = pt.x;
		this->top = pt.y;
		this->SetDirty();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_CARD_CAPTION) return CardTitle(this->target);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_CARD_INFO) {
			size.height = MAX_INFO_LINES * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.framerect.Vertical();
			size.width = std::max<uint>(size.width, ScaleGUITrad(200));
		} else if (widget >= WID_CARD_ACTION_FIRST) {
			int t = Touch::TouchTargetPx();
			size.width = std::max<uint>(size.width, t);
			size.height = std::max<uint>(size.height, t);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_CARD_INFO) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		for (const std::string &line : CardInfo(this->target)) {
			DrawString(ir, line);
			ir.top += GetCharacterHeight(FontSize::Normal);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget < WID_CARD_ACTION_FIRST || widget >= WID_CARD_ACTION_FIRST + static_cast<int>(this->actions.size())) return;
		const CardTarget t = this->target;
		const Vehicle *v = Vehicle::GetIfValid(t.vehicle);

		switch (this->actions[widget - WID_CARD_ACTION_FIRST]) {
			case CardAction::StartStop:
				if (v != nullptr) StartStopVehicle(v, true);
				this->SetDirty();
				return;
			case CardAction::ToDepot:
				if (v != nullptr) Command<Commands::SendVehicleToDepot>::Post(GetCmdSendToDepotMsg(v), v->index, {}, {});
				return;
			case CardAction::Follow:
				if (v != nullptr) {
					Window *main = GetMainWindow();
					ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos);
					main->viewport->follow_vehicle = v->index;
				}
				return;
			case CardAction::Orders:
				if (v != nullptr) ShowOrdersWindow(v);
				break;
			case CardAction::Centre:
				if (t.kind == CardKind::Station && Station::IsValidID(t.station)) ScrollMainWindowToTile(Station::Get(t.station)->xy);
				if (t.kind == CardKind::Town && Town::IsValidID(t.town)) ScrollMainWindowToTile(Town::Get(t.town)->xy);
				if (t.kind == CardKind::Industry && Industry::IsValidID(t.industry)) ScrollMainWindowToTile(Industry::Get(t.industry)->location.tile);
				return;
			case CardAction::Buy:
				if (IsValidTile(t.tile) && IsDepotTile(t.tile)) ShowBuildVehicleWindow(t.tile, DepotType(t.tile));
				break;
			case CardAction::Vehicles:
				if (t.kind == CardKind::Station && Station::IsValidID(t.station)) {
					/* Vehicles of the station's main mode; the list can switch types itself. */
					const Station *st = Station::Get(t.station);
					VehicleType vt = st->facilities.Test(StationFacility::Train) ? VehicleType::Train :
							st->facilities.Any({StationFacility::BusStop, StationFacility::TruckStop}) ? VehicleType::Road :
							st->facilities.Test(StationFacility::Dock) ? VehicleType::Ship : VehicleType::Aircraft;
					ShowVehicleListWindow(_local_company, vt, t.station);
				} else if (t.kind == CardKind::Depot && IsValidTile(t.tile) && IsDepotTile(t.tile)) {
					ShowVehicleListWindow(_local_company, DepotType(t.tile), t.tile);
				}
				break;
			case CardAction::More:
				switch (t.kind) {
					case CardKind::Vehicle: if (v != nullptr) ShowVehicleViewWindow(v); break;
					case CardKind::Station: if (Station::IsValidID(t.station)) ShowStationViewWindow(t.station); break;
					case CardKind::Depot: if (IsValidTile(t.tile) && IsDepotTile(t.tile)) ShowDepotWindow(t.tile, DepotType(t.tile)); break;
					case CardKind::Town: if (Town::IsValidID(t.town)) ShowTownViewWindow(t.town); break;
					case CardKind::Industry: if (Industry::IsValidID(t.industry)) ShowIndustryViewWindow(t.industry); break;
				}
				break;
		}
		/* The action opened a window: the card has done its job. */
		this->Close();
	}

	/** Live data: redraw twice a second; close when the target is gone. */
	void OnRealtimeTick(uint delta_ms) override
	{
		this->refresh_ms += delta_ms;
		if (this->refresh_ms < 500) return;
		this->refresh_ms = 0;
		if (CardTitle(this->target).empty()) {
			this->Close();
			return;
		}
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_card_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CARD_CAPTION), SetResize(1, 0),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_CARD_INFO), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
	NWidgetFunction(MakeActionRow),
};

static WindowDesc _card_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileCard, WindowClass::None,
	{},
	_nested_card_widgets
);

static void OpenCard(const CardTarget &t)
{
	CloseWindowById(WindowClass::MobileCard, 0);
	_card = t;
	new CardWindow(_card_desc);
}

} // namespace

namespace Touch {

bool ShowCardForClick(const Viewport &vp, int x, int y, const Vehicle *v)
{
	CardTarget t;
	if (v != nullptr && IsCompanyBuildableVehicleType(v)) {
		t.kind = CardKind::Vehicle;
		t.vehicle = v->First()->index;
		OpenCard(t);
		return true;
	}

	Point pt = TranslateXYToTileCoord(vp, x, y);
	if (pt.x < 0 || pt.y < 0) return false;
	TileIndex tile = TileVirtXY(pt.x, pt.y);
	if (!IsValidTile(tile)) return false;

	if (IsDepotTile(tile)) {
		t.kind = CardKind::Depot;
		t.tile = tile;
	} else if (IsTileType(tile, TileType::Station) && Station::IsValidID(GetStationIndex(tile))) {
		t.kind = CardKind::Station;
		t.station = GetStationIndex(tile);
	} else if (IsTileType(tile, TileType::House)) {
		t.kind = CardKind::Town;
		t.town = GetTownIndex(tile);
	} else if (IsTileType(tile, TileType::Industry)) {
		t.kind = CardKind::Industry;
		t.industry = GetIndustryIndex(tile);
	} else {
		return false;
	}
	OpenCard(t);
	return true;
}

} // namespace Touch
