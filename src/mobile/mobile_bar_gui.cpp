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
#include "build_palette_gui.h"
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
#include "../smallmap_gui.h"
#include "../viewport_func.h"
#include "../station_gui.h"
#include "../station_func.h"
#include "../town.h"
#include "../station_map.h"
#include "../town_map.h"
#include "../tile_map.h"
#include "../map_func.h"
#include "../landscape_cmd.h"
#include "../gui.h"
#include "../openttd.h"
#include "../command_func.h"
#include "../misc_cmd.h"
#include "../network/network.h"
#include "../sound_func.h"
#include "../rail_type.h"
#include "../road_type.h"
#include "../zoom_func.h"
#include "../table/sprites.h"
#include "../core/math_func.hpp"

#include "table/strings.h"

#include "../safeguards.h"

extern RailType _last_built_railtype;
extern RoadType _last_built_roadtype;
extern RoadType _last_built_tramtype;

namespace {

enum MobileBarWidgets : WidgetID {
	WID_MB_BUILD,
	WID_MB_VEHICLES,
	WID_MB_MAP,
	WID_MB_FINANCE,
	WID_MB_MENU,
	WID_MB_CONTEXT, ///< Not a real widget: id of the long-press map context menu dropdown.
};

enum class VehicleEntry : int { Trains, RoadVehicles, Ships, Aircraft };
enum class MenuEntry : int { ToggleToolbar, Pause, Save, Load, Settings, ExitToMenu };
enum class ContextEntry : int { Info, Centre, Station, Town, Demolish };

static TileIndex _context_tile = INVALID_TILE; ///< Tile the long-press context menu was opened on.

/** Minimum button size in GUI pixels; at the mobile GUI scale one GUI pixel is one dp, so ~48dp targets. */

struct MobileBarWindow : Window {
	MobileBarWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		RepositionMobileBar();
	}

	Point OnInitialPosition([[maybe_unused]] int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return GetMobileBarPosition(sm_width, sm_height);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget < WID_MB_BUILD || widget > WID_MB_MENU) return;
		int t = Touch::TouchTargetPx();
		size.width = std::max<uint>(size.width, t);
		size.height = std::max<uint>(size.height, t);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		DropDownList list;
		switch (widget) {
			case WID_MB_BUILD:
				/* Icon row of build modes in place of the navigation (see build_palette_gui.cpp). */
				ShowBuildPalette(PaletteMode::Chooser);
				return;

			case WID_MB_VEHICLES:
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_TRAINS, VehicleEntry::Trains));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_ROAD, VehicleEntry::RoadVehicles));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_SHIPS, VehicleEntry::Ships));
				list.push_back(MakeDropDownListStringItem(STR_MOBILE_VEHICLES_AIRCRAFT, VehicleEntry::Aircraft));
				break;

			case WID_MB_MAP:
				ShowSmallMap();
				return;

			case WID_MB_FINANCE:
				ShowCompanyFinances(_local_company);
				return;

			case WID_MB_MENU:
				list.push_back(MakeDropDownListStringItem(Touch::IsToolbarHidden() ? STR_MOBILE_MENU_SHOW_TOOLBAR : STR_MOBILE_MENU_HIDE_TOOLBAR, MenuEntry::ToggleToolbar));
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

	void OnDropdownClose(Point pt, WidgetID widget, int index, int click_result, bool instant_close) override
	{
		if (widget == WID_MB_CONTEXT) return; // no button to raise
		this->Window::OnDropdownClose(pt, widget, index, click_result, instant_close);
	}

	void OnDropdownSelect(WidgetID widget, int index, [[maybe_unused]] int click_result) override
	{
		switch (widget) {
			case WID_MB_CONTEXT:
				if (!IsValidTile(_context_tile)) break;
				switch (static_cast<ContextEntry>(index)) {
					case ContextEntry::Info: ShowLandInfo(_context_tile); break;
					case ContextEntry::Centre: ScrollMainWindowToTile(_context_tile); break;
					case ContextEntry::Station: ShowStationViewWindow(GetStationIndex(_context_tile)); break;
					case ContextEntry::Town: ShowTownViewWindow(GetTownIndex(_context_tile)); break;
					case ContextEntry::Demolish:
						Command<Commands::LandscapeClear>::Post(STR_ERROR_CAN_T_CLEAR_THIS_AREA, _context_tile);
						break;
				}
				break;

			case WID_MB_VEHICLES: {
				static constexpr VehicleType types[] = {VehicleType::Train, VehicleType::Road, VehicleType::Ship, VehicleType::Aircraft};
				if (index >= 0 && index < static_cast<int>(std::size(types))) ShowVehicleListWindow(_local_company, types[index]);
				break;
			}

			case WID_MB_MENU:
				switch (static_cast<MenuEntry>(index)) {
					case MenuEntry::ToggleToolbar:
						Touch::SetToolbarHidden(!Touch::IsToolbarHidden());
						break;
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

/** Icons only (names are tooltips, shown on long-press); each a finger-sized square, together spanning the width. */
static constexpr std::initializer_list<NWidgetPart> _nested_mobile_bar_widgets = {
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MB_BUILD), SetFill(1, 1), SetResize(1, 0), SetSpriteTip(SPR_IMG_BUILDRAIL, STR_MOBILE_BAR_BUILD),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MB_VEHICLES), SetFill(1, 1), SetResize(1, 0), SetSpriteTip(SPR_IMG_TRAINLIST, STR_MOBILE_BAR_VEHICLES),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MB_MAP), SetFill(1, 1), SetResize(1, 0), SetSpriteTip(SPR_IMG_SMALLMAP, STR_MOBILE_BAR_MAP),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MB_FINANCE), SetFill(1, 1), SetResize(1, 0), SetSpriteTip(SPR_IMG_COMPANY_FINANCE, STR_MOBILE_BAR_FINANCE),
		NWidget(WWT_PUSHIMGBTN, Colours::Grey, WID_MB_MENU), SetFill(1, 1), SetResize(1, 0), SetSpriteTip(SPR_IMG_SETTINGS, STR_MOBILE_BAR_MENU),
	EndContainer(),
};

static WindowDesc _mobile_bar_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileBar, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_bar_widgets
);

enum MobileConfirmWidgets : WidgetID {
	WID_MC_BUILD,
	WID_MC_CANCEL,
};

/**
 * Build / Cancel buttons shown after a construction drag on touch. Touches on it are handled by
 * the touch layer (Touch::HandleFinger) before reaching the window system, because a normal click
 * would move the cursor and thereby change the pending selection.
 */
struct MobileConfirmWindow : Window {
	Point anchor;

	MobileConfirmWindow(WindowDesc &desc, Point anchor) : Window(desc), anchor(anchor)
	{
		this->InitNested(0);
	}

	/** Above the finger (so it doesn't cover the end point), kept inside the safe area. */
	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		const Touch::SafeInsets &safe = Touch::GetSafeInsets();
		int gap = ScaleGUITrad(40);
		int x = this->anchor.x - sm_width / 2;
		int y = this->anchor.y - gap - sm_height;
		if (y < safe.top) y = this->anchor.y + gap;
		x = Clamp(x, safe.left, _screen.width - safe.right - sm_width);
		y = Clamp(y, safe.top, _screen.height - safe.bottom - sm_height);
		return {x, y};
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_MC_BUILD && widget != WID_MC_CANCEL) return;
		int t = Touch::TouchTargetPx();
		size.width = std::max<uint>(size.width, 2 * t);
		size.height = std::max<uint>(size.height, t);
	}

	ConfirmHit HitTest(int x, int y) const
	{
		for (WidgetID id : {WID_MC_BUILD, WID_MC_CANCEL}) {
			Rect r = this->GetWidget<NWidgetBase>(id)->GetCurrentRect().Translate(this->left, this->top);
			if (r.Contains({x, y})) return id == WID_MC_BUILD ? ConfirmHit::Build : ConfirmHit::Cancel;
		}
		return ConfirmHit::None;
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_mobile_confirm_widgets = {
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_MC_BUILD), SetFill(1, 1), SetStringTip(STR_MOBILE_CONFIRM_BUILD),
		NWidget(WWT_PUSHTXTBTN, Colours::Red, WID_MC_CANCEL), SetFill(1, 1), SetStringTip(STR_MOBILE_CONFIRM_CANCEL),
	EndContainer(),
};

static WindowDesc _mobile_confirm_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileConfirm, WindowClass::None,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_mobile_confirm_widgets
);

} // namespace

void ShowBuildConfirm(int x, int y)
{
	CloseBuildConfirm();
	new MobileConfirmWindow(_mobile_confirm_desc, {x, y});
}

void CloseBuildConfirm()
{
	CloseWindowById(WindowClass::MobileConfirm, 0);
}

ConfirmHit GetBuildConfirmHit(int x, int y)
{
	const MobileConfirmWindow *w = dynamic_cast<const MobileConfirmWindow *>(FindWindowById(WindowClass::MobileConfirm, 0));
	return w != nullptr ? w->HitTest(x, y) : ConfirmHit::None;
}

/**
 * Bottom of the action row: above the gesture/navigation inset (where taps are unreliable) and above
 * the status bar. The status bar itself is information only, so it may sit inside that inset.
 */
int GetActionRowBottom()
{
	int bottom = _screen.height - Touch::GetSafeInsets().bottom;
	if (const Window *status = FindWindowById(WindowClass::Statusbar, 0); status != nullptr) bottom = std::min(bottom, status->top);
	return bottom;
}

/** In the action row, or parked below the screen while a build palette occupies that row. */
Point GetMobileBarPosition(int width, int height)
{
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	int area_width = _screen.width - safe.left - safe.right;
	if (IsBuildPaletteOpen()) return {safe.left, _screen.height + 1};
	return {safe.left + (area_width - width) / 2, GetActionRowBottom() - height};
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
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	int area_width = _screen.width - safe.left - safe.right;
	if (w->width != area_width) ResizeWindow(w, area_width - w->width, 0, false);
	Point pt = GetMobileBarPosition(w->width, w->height);
	w->SetDirty();
	w->left = pt.x;
	w->top = pt.y;
	w->SetDirty();
	RepositionBuildPalette();
}

/**
 * Long-press on the map (no construction tool active): a context menu for the tile under the finger.
 * @return Whether the menu was shown (otherwise the caller falls back to a right click).
 */
bool ShowMapContextMenu(int x, int y)
{
	Window *bar = FindWindowById(WindowClass::MobileBar, 0);
	Window *main = GetMainWindow();
	if (bar == nullptr || main == nullptr || FindWindowFromPt(x, y) != main) return false;
	const Viewport *vp = IsPtInWindowViewport(main, x, y);
	if (vp == nullptr) return false;
	Point pt = TranslateXYToTileCoord(*vp, x, y);
	if (pt.x < 0 || pt.y < 0) return false;
	TileIndex tile = TileVirtXY(pt.x, pt.y);
	if (!IsValidTile(tile)) return false;
	_context_tile = tile;

	DropDownList list;
	list.push_back(MakeDropDownListStringItem(STR_MOBILE_CONTEXT_INFO, ContextEntry::Info));
	list.push_back(MakeDropDownListStringItem(STR_MOBILE_CONTEXT_CENTRE, ContextEntry::Centre));
	if (IsTileType(tile, TileType::Station)) list.push_back(MakeDropDownListStringItem(STR_MOBILE_CONTEXT_STATION, ContextEntry::Station));
	if (IsTileType(tile, TileType::House)) list.push_back(MakeDropDownListStringItem(STR_MOBILE_CONTEXT_TOWN, ContextEntry::Town));
	list.push_back(MakeDropDownListStringItem(STR_MOBILE_CONTEXT_DEMOLISH, ContextEntry::Demolish));

	/* Anchor at the finger (relative to the bar, the dropdown's parent). */
	Rect anchor{x - bar->left, y - bar->top, x - bar->left + 1, y - bar->top + 1};
	ShowDropDownListAt(bar, std::move(list), -1, WID_MB_CONTEXT, anchor, Colours::Grey);
	return true;
}
