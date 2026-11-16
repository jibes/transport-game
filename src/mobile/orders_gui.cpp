/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file orders_gui.cpp Touch stop picking.
 *
 * Which stations a vehicle serves is the player's decision; typing it into the orders window is
 * not. Here the player taps stations on the map in order; each tap appends a "go to" order
 * (InsertOrder command). The sheet lists the stops so far, with "Remove last" and "Done".
 * Load/unload options and timetables stay in the full orders window ("More" on the card).
 */

#include "../stdafx.h"
#include "orders_gui.h"
#include "touch_input.h"
#include "mobile_bar_gui.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../viewport_func.h"
#include "../command_func.h"
#include "../order_cmd.h"
#include "../order_base.h"
#include "../vehicle_base.h"
#include "../station_base.h"
#include "../station_map.h"
#include "../tile_map.h"
#include "../map_func.h"
#include "../strings_func.h"
#include "../gfx_func.h"
#include "../zoom_func.h"
#include "../company_func.h"

#include "table/strings.h"

#include "../safeguards.h"

namespace Touch {

namespace {

static VehicleID _picking_for = VehicleID::Invalid();
static constexpr int MAX_LISTED_STOPS = 4;

enum StopPickWidgets : WidgetID {
	WID_SP_CLOSE,
	WID_SP_CAPTION,
	WID_SP_LIST,
	WID_SP_REMOVE_LAST,
	WID_SP_DONE,
};

struct StopPickWindow : Window {
	StopPickWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		const SafeInsets &safe = GetSafeInsets();
		int area_width = _screen.width - safe.left - safe.right;
		if (this->width < area_width) ResizeWindow(this, area_width - this->width, 0, false);
		Point pt = GetSheetPosition(this->width, this->height);
		this->left = pt.x;
		this->top = pt.y;
	}

	void Close(int data = 0) override
	{
		_picking_for = VehicleID::Invalid();
		this->Window::Close(data);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_SP_CAPTION) {
			if (!Vehicle::IsValidID(_picking_for)) return {};
			return GetString(STR_MOBILE_STOPS_CAPTION, _picking_for);
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_SP_LIST) {
			size.height = (MAX_LISTED_STOPS + 1) * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.framerect.Vertical();
			size.width = std::max<uint>(size.width, ScaleGUITrad(200));
		} else if (widget == WID_SP_REMOVE_LAST || widget == WID_SP_DONE) {
			int t = TouchTargetPx();
			size.width = std::max<uint>(size.width, 2 * t);
			size.height = std::max<uint>(size.height, t);
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_SP_LIST) return;
		const Vehicle *v = Vehicle::GetIfValid(_picking_for);
		if (v == nullptr) return;
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int line = GetCharacterHeight(FontSize::Normal);
		DrawString(ir, STR_MOBILE_STOPS_HINT, TextColour::Black);
		ir.top += line;
		auto orders = v->Orders();
		/* Most recent stops are the interesting ones while picking. */
		size_t first = orders.size() > MAX_LISTED_STOPS ? orders.size() - MAX_LISTED_STOPS : 0;
		for (size_t i = first; i < orders.size(); i++) {
			const Order &o = orders[i];
			if (!o.IsType(OT_GOTO_STATION)) continue;
			DrawString(ir, GetString(STR_MOBILE_STOPS_ENTRY, i + 1, o.GetDestination().ToStationID()));
			ir.top += line;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Vehicle *v = Vehicle::GetIfValid(_picking_for);
		switch (widget) {
			case WID_SP_REMOVE_LAST:
				if (v != nullptr && v->GetNumOrders() > 0) {
					Command<Commands::DeleteOrder>::Post(STR_ERROR_CAN_T_DELETE_THIS_ORDER, v->tile, v->index, v->GetNumOrders() - 1);
				}
				break;
			case WID_SP_DONE:
				this->Close();
				break;
		}
	}

	/** Order list changes arrive asynchronously (commands); refresh regularly. */
	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		if (!Vehicle::IsValidID(_picking_for)) {
			this->Close();
			return;
		}
		this->SetWidgetDirty(WID_SP_LIST);
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_stop_pick_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey, WID_SP_CLOSE),
		NWidget(WWT_CAPTION, Colours::Grey, WID_SP_CAPTION), SetResize(1, 0),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_SP_LIST), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_SP_REMOVE_LAST), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STOPS_REMOVE_LAST),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_SP_DONE), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STOPS_DONE),
	EndContainer(),
};

static WindowDesc _stop_pick_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileStops, WindowClass::None,
	{},
	_nested_stop_pick_widgets
);

} // namespace

void StartStopPicking(VehicleID vehicle)
{
	const Vehicle *v = Vehicle::GetIfValid(vehicle);
	if (v == nullptr || v->owner != _local_company) return;
	CloseWindowById(WindowClass::MobileStops, 0);
	_picking_for = vehicle;
	new StopPickWindow(_stop_pick_desc);
}

bool HandleStopTap(const Viewport &vp, int x, int y)
{
	const Vehicle *v = Vehicle::GetIfValid(_picking_for);
	if (v == nullptr) return false;
	Point pt = TranslateXYToTileCoord(vp, x, y);
	if (pt.x < 0 || pt.y < 0) return true;
	TileIndex tile = TileVirtXY(pt.x, pt.y);
	if (!IsValidTile(tile) || !IsTileType(tile, TileType::Station) || !Station::IsValidID(GetStationIndex(tile))) {
		return true; // only stations count; ignore other taps rather than opening cards
	}
	Order order{};
	order.MakeGoToStation(GetStationIndex(tile));
	Command<Commands::InsertOrder>::Post(STR_ERROR_CAN_T_INSERT_NEW_ORDER, v->tile, v->index, v->GetNumOrders(), order);
	return true;
}

} // namespace Touch
