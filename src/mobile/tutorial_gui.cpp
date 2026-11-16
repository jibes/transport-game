/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file tutorial_gui.cpp First-game guide for touch play.
 *
 * A one-line banner at the top shows the next step towards a first running train line and where
 * to find the tool for it. Steps are recognised from the game state (not from button presses), so
 * any way of doing them counts and nothing is done for the player. It disappears when the line
 * runs or when dismissed, and is not shown again.
 */

#include "../stdafx.h"
#include "tutorial_gui.h"
#include "touch_input.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../settings_type.h"
#include "../strings_func.h"
#include "../gfx_func.h"
#include "../station_base.h"
#include "../vehicle_base.h"
#include "../rail_map.h"
#include "../company_func.h"
#include "../openttd.h"
#include "../table/sprites.h"

#include "table/strings.h"

#include "../safeguards.h"

namespace Touch {

namespace {

enum GuideWidgets : WidgetID {
	WID_GD_CLOSE,
	WID_GD_TEXT,
};

static constexpr int NUM_STEPS = 4;

/** Index of the first step not yet achieved (NUM_STEPS = all done). */
static int CurrentStep()
{
	int rail_stations = 0;
	for (const Station *st : Station::Iterate()) {
		if (st->owner == _local_company && st->facilities.Test(StationFacility::Train)) rail_stations++;
	}
	if (rail_stations < 2) return 0;

	int own_rail = 0;
	for (const auto t : Map::Iterate()) {
		if (IsPlainRailTile(t) && GetTileOwner(t) == _local_company && ++own_rail >= 3) break;
	}
	if (own_rail < 3) return 1;

	const Vehicle *train = nullptr;
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type == VehicleType::Train && v->IsPrimaryVehicle() && v->owner == _local_company) {
			train = v;
			if (v->GetNumOrders() >= 2 && !v->vehstatus.Test(VehState::Stopped)) return NUM_STEPS;
		}
	}
	return train == nullptr ? 2 : 3;
}

static StringID StepText(int step)
{
	static constexpr StringID TEXTS[] = {
		STR_MOBILE_GUIDE_STEP_STATIONS, STR_MOBILE_GUIDE_STEP_CONNECT, STR_MOBILE_GUIDE_STEP_TRAIN, STR_MOBILE_GUIDE_STEP_STOPS, STR_MOBILE_GUIDE_DONE,
	};
	return TEXTS[step];
}

struct GuideWindow : Window {
	int step = 0;
	uint ms_since_check = 0;
	uint ms_since_done = 0;

	GuideWindow(WindowDesc &desc) : Window(desc)
	{
		this->step = CurrentStep();
		this->InitNested(0);
		const SafeInsets &safe = GetSafeInsets();
		int area_width = _screen.width - safe.left - safe.right;
		if (this->width < area_width) ResizeWindow(this, area_width - this->width, 0, false);
		this->left = safe.left;
		this->top = safe.top;
	}

	/** Dismissed by the player or finished: don't show again. (Game cleanups skip this NoClose window.) */
	void Finish()
	{
		_settings_client.gui.mobile_guide_done = true;
		this->Close();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_GD_CLOSE) this->Finish();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_GD_TEXT) {
			if (this->step >= NUM_STEPS) return GetString(STR_MOBILE_GUIDE_DONE);
			return GetString(STR_MOBILE_GUIDE_STEP, this->step + 1, NUM_STEPS, StepText(this->step));
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_GD_CLOSE) {
			size.width = std::max<uint>(size.width, TouchTargetPx());
			size.height = std::max<uint>(size.height, TouchTargetPx());
		}
		if (widget == WID_GD_TEXT) {
			/* Long enough for the longest step; wraps are avoided by keeping texts short. */
			size.height = std::max<uint>(size.height, TouchTargetPx());
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (this->step >= NUM_STEPS) {
			this->ms_since_done += delta_ms;
			if (this->ms_since_done > 6000) this->Finish();
			return;
		}
		this->ms_since_check += delta_ms;
		if (this->ms_since_check < 1000) return;
		this->ms_since_check = 0;
		int now = CurrentStep();
		if (now != this->step) {
			this->step = now;
			this->SetDirty();
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_guide_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHIMGBTN, Colours::LightBlue, WID_GD_CLOSE), SetSpriteTip(SPR_CLOSEBOX, STR_TOOLTIP_CLOSE_WINDOW),
		NWidget(WWT_PANEL, Colours::LightBlue),
			NWidget(WWT_TEXT, Colours::Invalid, WID_GD_TEXT), SetFill(1, 1), SetResize(1, 0), SetAlignment({AlignmentH::Start, AlignmentV::Middle}), SetPadding(0, 4, 0, 4),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _guide_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileGuide, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose}, // own close button: game cleanups must not count as "dismissed"
	_nested_guide_widgets
);

} // namespace

void MaybeShowGuide()
{
	if (!AssistsEnabled() || _settings_client.gui.mobile_guide_done || _game_mode != GameMode::Normal) return;
	if (FindWindowById(WindowClass::MobileGuide, 0) != nullptr) return;
	new GuideWindow(_guide_desc);
}

} // namespace Touch
