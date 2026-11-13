/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file mobile_bar_gui.cpp Bottom action bar for touch play: Build, Vehicles, Finance, Menu.
 *
 * Each button opens a short dropdown of the most used actions, with large touch targets,
 * replacing the 30 small icons of the main toolbar for everyday play. All actions call the
 * same functions as the main toolbar, so behaviour and multiplayer compatibility are unchanged.
 */

#include "../stdafx.h"
#include "mobile_bar_gui.h"
#include "touch_input.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../gfx_func.h"
#include "../dropdown_func.h"
#include "../dropdown_type.h"
#include "../company_func.h"
#include "../company_gui.h"
#include "../vehicle_gui.h"
#include "../rail_gui.h"
#include "../road_gui.h"
#include "../terraform_gui.h"
#include "../fios.h"
#include "../gui.h"
#include "../openttd.h"
#include "../command_func.h"
#include "../misc_cmd.h"
#include "../network/network.h"
#include "../sound_func.h"
#include "../rail_type.h"
#include "../road_type.h"

#include "table/strings.h"

#include "../safeguards.h"

extern RailType _last_built_railtype;
extern RoadType _last_built_roadtype;
extern RoadType _last_built_tramtype;

namespace {

enum MobileBarWidgets : WidgetID {
	WID_MB_BUILD,
	WID_MB_VEHICLES,
	WID_MB_FINANCE,
	WID_MB_MENU,
};

enum class BuildEntry : int { Rail, Road, Tram, Water, Air, Landscape, Trees };
enum class VehicleEntry : int { Trains, RoadVehicles, Ships, Aircraft };
enum class MenuEntry : int { Pause, Save, Load, Settings, ExitToMenu };

/** Minimum button size in GUI pixels; at the mobile GUI scale one GUI pixel is one dp, so ~48dp targets. */
static constexpr int BUTTON_WIDTH = 76;
static constexpr int BUTTON_HEIGHT = 30;

struct MobileBarWindow : Window {
	MobileBarWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
	}

	Point OnInitialPosition([[maybe_unused]] int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return GetMobileBarPosition(sm_width, sm_height);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		DropDownList list;
		switch (widget) {
			case WID_MB_BUILD:
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_RAIL, BuildEntry::Rail));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_ROAD, BuildEntry::Road));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_TRAM, BuildEntry::Tram));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_WATER, BuildEntry::Water));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_AIR, BuildEntry::Air));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_LANDSCAPE, BuildEntry::Landscape));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_BUILD_TREES, BuildEntry::Trees));
				break;

			case WID_MB_VEHICLES:
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_TRAINS, VehicleEntry::Trains));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_ROAD, VehicleEntry::RoadVehicles));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_SHIPS, VehicleEntry::Ships));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_AIRCRAFT, VehicleEntry::Aircraft));
				break;

			case WID_MB_FINANCE:
				ShowCompanyFinances(_local_company);
				return;

			case WID_MB_MENU:
				list.push_back(MakeDropDownListStringItem(_pause_mode.Any() ? STR_MOBILE_MENU_RESUME : STR_MOBILE_MENU_PAUSE, MenuEntry::Pause));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_MENU_SAVE, MenuEntry::Save));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_MENU_LOAD, MenuEntry::Load));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_MENU_SETTINGS, MenuEntry::Settings));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_MENU_EXIT, MenuEntry::ExitToMenu));
				break;

			default:
				return;
		}
		ShowDropDownList(this, std::move(list), -1, widget);
	}

	void OnDropdownSelect(WidgetID widget, int index, [[maybe_unused]] int click_result) override
	{
		switch (widget) {
			case WID_MB_BUILD:
				switch (static_cast<BuildEntry>(index)) {
					case BuildEntry::Rail: ShowBuildRailToolbar(_last_built_railtype); break;
					case BuildEntry::Road: ShowBuildRoadToolbar(_last_built_roadtype); break;
					case BuildEntry::Tram: ShowBuildRoadToolbar(_last_built_tramtype); break;
					case BuildEntry::Water: ShowBuildDocksToolbar(); break;
					case BuildEntry::Air: ShowBuildAirToolbar(); break;
					case BuildEntry::Landscape: ShowTerraformToolbar(nullptr); break;
					case BuildEntry::Trees: ShowBuildTreesToolbar(); break;
				}
				break;

			case WID_MB_VEHICLES: {
				static constexpr VehicleType types[] = {VehicleType::Train, VehicleType::Road, VehicleType::Ship, VehicleType::Aircraft};
				if (index >= 0 && index < static_cast<int>(std::size(types))) ShowVehicleListWindow(_local_company, types[index]);
				break;
			}

			case WID_MB_MENU:
				switch (static_cast<MenuEntry>(index)) {
					case MenuEntry::Pause:
						if (_networking && !_network_server) break; // only the server can pause
						if (Command<Commands::Pause>::Post(PauseMode::Normal, _pause_mode.None())) SndConfirmBeep();
						break;
					case MenuEntry::Save: ShowSaveLoadDialog(AbstractFileType::Savegame, SaveLoadOperation::Save); break;
					case MenuEntry::Load: ShowSaveLoadDialog(AbstractFileType::Savegame, SaveLoadOperation::Load); break;
					case MenuEntry::Settings: ShowGameOptions(); break;
					case MenuEntry::ExitToMenu: AskExitToGameMenu(); break;
				}
				break;
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_bar_widgets = {
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MB_BUILD), SetMinimalSize(BUTTON_WIDTH, BUTTON_HEIGHT), SetFill(1, 1), SetStringTip(STR_MOBILE_BAR_BUILD),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MB_VEHICLES), SetMinimalSize(BUTTON_WIDTH, BUTTON_HEIGHT), SetFill(1, 1), SetStringTip(STR_MOBILE_BAR_VEHICLES),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MB_FINANCE), SetMinimalSize(BUTTON_WIDTH, BUTTON_HEIGHT), SetFill(1, 1), SetStringTip(STR_MOBILE_BAR_FINANCE),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_MB_MENU), SetMinimalSize(BUTTON_WIDTH, BUTTON_HEIGHT), SetFill(1, 1), SetStringTip(STR_MOBILE_BAR_MENU),
	EndContainer(),
};

static WindowDesc _mobile_bar_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileBar, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_bar_widgets
);

} // namespace

/** Bottom centre of the safe area, directly above the status bar. */
Point GetMobileBarPosition(int width, int height)
{
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	const Window *status = FindWindowById(WindowClass::Statusbar, 0);
	int bottom = _screen.height - safe.bottom - (status != nullptr ? status->height : 0);
	int area_width = _screen.width - safe.left - safe.right;
	return {safe.left + (area_width - width) / 2, bottom - height};
}

void ShowMobileBar()
{
	if (_game_mode != GameMode::Normal) return;
	if (FindWindowById(WindowClass::MobileBar, 0) != nullptr) return;
	new MobileBarWindow(_mobile_bar_desc);
}

void RepositionMobileBar()
{
	Window *w = FindWindowById(WindowClass::MobileBar, 0);
	if (w == nullptr) return;
	Point pt = GetMobileBarPosition(w->width, w->height);
	w->SetDirty();
	w->left = pt.x;
	w->top = pt.y;
	w->SetDirty();
}
