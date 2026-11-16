/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file station_sheet_gui.cpp Compact touch controls for the rail station picker.
 *
 * The picker's rows of 1..7 buttons are replaced by one row of finger-sized controls (rotate,
 * tracks -/+, length -/+). The picker stays the source of truth: it is parked off-screen and the
 * sheet forwards to its buttons, so station classes, custom stations and placement are unchanged.
 * Coverage highlighting is switched on, so the catchment shows live while placing.
 */

#include "../stdafx.h"
#include "station_sheet_gui.h"
#include "touch_input.h"
#include "mobile_bar_gui.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../settings_type.h"
#include "../strings_func.h"
#include "../gfx_func.h"
#include "../widgets/rail_widget.h"
#include "../core/math_func.hpp"

#include "table/strings.h"

#include "../safeguards.h"

namespace Touch {

namespace {

enum StationSheetWidgets : WidgetID {
	WID_SS_INFO,
	WID_SS_ROTATE,
	WID_SS_TRACKS_LESS,
	WID_SS_TRACKS_MORE,
	WID_SS_LENGTH_LESS,
	WID_SS_LENGTH_MORE,
};

static constexpr int MAX_STATION_SIZE = 7;

/** The rail station picker (a child of the rail toolbar), if open. */
static Window *GetRailStationPicker(Window *rail_toolbar)
{
	for (Window *w : Window::Iterate()) {
		if (w->window_class == WindowClass::BuildStation && w->parent == rail_toolbar) return w;
	}
	return nullptr;
}

static void Park(Window *w)
{
	int park_top = -w->height - 1;
	if (w->top == park_top) return;
	w->SetDirty();
	w->top = park_top;
}

struct StationSheetWindow : Window {
	StationSheetWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		const SafeInsets &safe = GetSafeInsets();
		int area_width = _screen.width - safe.left - safe.right;
		if (this->width < area_width) ResizeWindow(this, area_width - this->width, 0, false);
		Point pt = GetSheetPosition(this->width, this->height);
		this->left = pt.x;
		this->top = pt.y;
	}

	Window *Picker() const
	{
		return GetRailStationPicker(FindWindowById(WindowClass::BuildToolbar, TransportType::Rail));
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_SS_INFO) {
			return GetString(STR_MOBILE_STATION_SIZE, _settings_client.gui.station_numtracks, _settings_client.gui.station_platlength);
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_SS_INFO) return;
		int t = TouchTargetPx();
		size.width = std::max<uint>(size.width, t);
		size.height = std::max<uint>(size.height, t);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		Window *picker = this->Picker();
		if (picker == nullptr) return;
		int tracks = _settings_client.gui.station_numtracks;
		int length = _settings_client.gui.station_platlength;
		switch (widget) {
			case WID_SS_ROTATE:
				picker->OnClick({0, 0}, picker->IsWidgetLowered(WID_BRAS_PLATFORM_DIR_X) ? WID_BRAS_PLATFORM_DIR_Y : WID_BRAS_PLATFORM_DIR_X, 1);
				break;
			case WID_SS_TRACKS_LESS:
			case WID_SS_TRACKS_MORE:
				tracks = Clamp(tracks + (widget == WID_SS_TRACKS_MORE ? 1 : -1), 1, MAX_STATION_SIZE);
				picker->OnClick({0, 0}, WID_BRAS_PLATFORM_NUM_BEGIN + tracks, 1);
				break;
			case WID_SS_LENGTH_LESS:
			case WID_SS_LENGTH_MORE:
				length = Clamp(length + (widget == WID_SS_LENGTH_MORE ? 1 : -1), 1, MAX_STATION_SIZE);
				picker->OnClick({0, 0}, WID_BRAS_PLATFORM_LEN_BEGIN + length, 1);
				break;
			default:
				return;
		}
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_station_sheet_widgets = {
	NWidget(WWT_PANEL, Colours::DarkGreen),
		NWidget(WWT_LABEL, Colours::Invalid, WID_SS_INFO), SetFill(1, 0), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_SS_ROTATE), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STATION_ROTATE),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_SS_TRACKS_LESS), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STATION_TRACKS_LESS),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_SS_TRACKS_MORE), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STATION_TRACKS_MORE),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_SS_LENGTH_LESS), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STATION_LENGTH_LESS),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_SS_LENGTH_MORE), SetFill(1, 1), SetResize(1, 0), SetStringTip(STR_MOBILE_STATION_LENGTH_MORE),
	EndContainer(),
};

static WindowDesc _station_sheet_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileStationSheet, WindowClass::None,
	{WindowDefaultFlag::NoClose}, // closes with the picker
	_nested_station_sheet_widgets
);

} // namespace

void SyncStationSheet(Window *rail_toolbar)
{
	Window *picker = rail_toolbar != nullptr ? GetRailStationPicker(rail_toolbar) : nullptr;
	Window *sheet = FindWindowById(WindowClass::MobileStationSheet, 0);
	if (picker == nullptr) {
		if (sheet != nullptr) sheet->Close();
		return;
	}
	Park(picker);
	if (sheet == nullptr) {
		/* Placing a station is about what it will serve: show the catchment while placing. */
		if (!_settings_client.gui.station_show_coverage) picker->OnClick({0, 0}, WID_BRAS_HIGHLIGHT_ON, 1);
		new StationSheetWindow(_station_sheet_desc);
	}
}

} // namespace Touch
