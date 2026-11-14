/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file build_palette_gui.cpp Touch build palette: a bottom sheet of large tool buttons.
 *
 * The regular construction toolbar keeps doing all the work (it is the callback window of the
 * placement tools and the parent of the station/depot pickers), but is parked above the screen.
 * The palette forwards taps to it and mirrors which tool is active, so game behaviour, hotkeys
 * and multiplayer are unchanged while every tool gets a finger-sized button with a label.
 */

#include "../stdafx.h"
#include "build_palette_gui.h"
#include "mobile_bar_gui.h"
#include "touch_input.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../gfx_func.h"
#include "../rail_gui.h"
#include "../rail_type.h"
#include "../transport_type.h"
#include "../widgets/rail_widget.h"
#include "../zoom_func.h"
#include "../core/math_func.hpp"

#include "table/strings.h"

#include "../safeguards.h"

extern RailType _last_built_railtype;

namespace {

enum BuildPaletteWidgets : WidgetID {
	WID_BP_CAPTION,
	WID_BP_TOOL_FIRST, ///< First tool button; tools follow in RAIL_TOOLS order.
};

struct PaletteTool {
	WidgetID toolbar_widget; ///< Widget of the construction toolbar this button forwards to.
	StringID label;
};

/** Most used rail tools; rarer ones (fixed-direction track, conversion) stay in the full toolbar. */
static constexpr PaletteTool RAIL_TOOLS[] = {
	{WID_RAT_AUTORAIL, STR_MOBILE_TOOL_TRACK},
	{WID_RAT_BUILD_STATION, STR_MOBILE_TOOL_STATION},
	{WID_RAT_BUILD_DEPOT, STR_MOBILE_TOOL_DEPOT},
	{WID_RAT_BUILD_SIGNALS, STR_MOBILE_TOOL_SIGNALS},
	{WID_RAT_BUILD_BRIDGE, STR_MOBILE_TOOL_BRIDGE},
	{WID_RAT_BUILD_TUNNEL, STR_MOBILE_TOOL_TUNNEL},
	{WID_RAT_BUILD_WAYPOINT, STR_MOBILE_TOOL_WAYPOINT},
	{WID_RAT_DEMOLISH, STR_MOBILE_TOOL_DEMOLISH},
	{WID_RAT_REMOVE, STR_MOBILE_TOOL_REMOVE},
};
static constexpr int NUM_RAIL_TOOLS = static_cast<int>(std::size(RAIL_TOOLS));

static constexpr int TOOL_WIDTH = 80;
static constexpr int TOOL_HEIGHT = 30;
static constexpr int TOOL_SLOT_WIDTH = 130; ///< GUI units per column (icon + label); decides the column count.

static int _palette_columns = 3; ///< Columns of the palette being built (MakeToolGrid reads it).

/** As many columns as fit the safe width: 2 in portrait, 3-5 in landscape. */
static int ColumnsForScreen()
{
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	int area_width = _screen.width - safe.left - safe.right;
	return Clamp(area_width / ScaleGUITrad(TOOL_SLOT_WIDTH), 2, 5);
}

/** The parked construction toolbar this palette drives, if still open. */
static Window *GetRailToolbar()
{
	return FindWindowById(WindowClass::BuildToolbar, TransportType::Rail);
}

/** Move a construction toolbar above the screen; it stays functional but out of the way. */
static void ParkToolbar(Window *w)
{
	if (w == nullptr) return;
	int park_top = -w->height - 1;
	if (w->top == park_top) return;
	w->SetDirty();
	if (w->viewport != nullptr) w->viewport->top += park_top - w->top;
	w->top = park_top;
}

struct BuildPaletteWindow : Window {
	int columns; ///< Layout this window was built with.
	bool keep_toolbar = false; ///< Closing only to rebuild the layout; leave the construction toolbar open.

	BuildPaletteWindow(WindowDesc &desc) : Window(desc), columns(_palette_columns)
	{
		this->CreateNestedTree();
		/* Same icons as the toolbar (they depend on the rail type). */
		if (const Window *rt = GetRailToolbar(); rt != nullptr) {
			for (int i = 0; i < NUM_RAIL_TOOLS; i++) {
				const NWidgetCore *src = rt->GetWidget<NWidgetCore>(RAIL_TOOLS[i].toolbar_widget);
				if (src != nullptr) this->GetWidget<NWidgetCore>(WID_BP_TOOL_FIRST + i)->SetSprite(src->GetSprite());
			}
		}
		this->FinishInitNested(0);
		RepositionBuildPalette();
	}

	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return GetBuildPalettePosition(sm_width, sm_height);
	}

	void Close(int data = 0) override
	{
		if (!this->keep_toolbar) CloseWindowById(WindowClass::BuildToolbar, TransportType::Rail);
		this->Window::Close(data);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, int click_count) override
	{
		if (widget < WID_BP_TOOL_FIRST || widget >= WID_BP_TOOL_FIRST + NUM_RAIL_TOOLS) return;
		Window *rt = GetRailToolbar();
		if (rt == nullptr) return;
		rt->OnClick({0, 0}, RAIL_TOOLS[widget - WID_BP_TOOL_FIRST].toolbar_widget, click_count);
		this->SyncLowered();
	}

	/** Mirror the toolbar's pressed tool, and follow it when it closes. */
	void OnMouseLoop() override
	{
		const Window *rt = GetRailToolbar();
		if (rt == nullptr) {
			this->Window::Close();
			return;
		}
		ParkToolbar(const_cast<Window *>(rt));
		this->SyncLowered();
	}

	void SyncLowered()
	{
		const Window *rt = GetRailToolbar();
		if (rt == nullptr) return;
		for (int i = 0; i < NUM_RAIL_TOOLS; i++) {
			bool lowered = rt->IsWidgetLowered(RAIL_TOOLS[i].toolbar_widget);
			if (this->IsWidgetLowered(WID_BP_TOOL_FIRST + i) != lowered) {
				this->SetWidgetLoweredState(WID_BP_TOOL_FIRST + i, lowered);
				this->SetWidgetDirty(WID_BP_TOOL_FIRST + i);
			}
		}
	}
};

static std::unique_ptr<NWidgetBase> MakeToolGrid()
{
	auto grid = std::make_unique<NWidgetVertical>(NWidContainerFlag::EqualSize);
	std::unique_ptr<NWidgetHorizontal> row;
	for (int i = 0; i < NUM_RAIL_TOOLS; i++) {
		if (i % _palette_columns == 0) row = std::make_unique<NWidgetHorizontal>(NWidContainerFlag::EqualSize);
		auto button = std::make_unique<NWidgetLeaf>(WWT_IMGTEXTBTN, Colours::DarkGreen, WID_BP_TOOL_FIRST + i, WidgetData{.string = RAIL_TOOLS[i].label}, STR_NULL);
		button->SetMinimalSize(TOOL_WIDTH, TOOL_HEIGHT);
		button->SetFill(1, 1);
		button->SetResize(1, 0);
		button->SetAlignment({AlignmentH::Start, AlignmentV::Middle});
		row->Add(std::move(button));
		if (i % _palette_columns == _palette_columns - 1) {
			grid->Add(std::move(row));
		} else if (i == NUM_RAIL_TOOLS - 1) {
			/* Pad the last row so its buttons keep the same width as the rows above. */
			for (int pad = i % _palette_columns + 1; pad < _palette_columns; pad++) {
				auto spacer = std::make_unique<NWidgetSpacer>(TOOL_WIDTH, TOOL_HEIGHT);
				spacer->SetFill(1, 1);
				spacer->SetResize(1, 0);
				row->Add(std::move(spacer));
			}
			grid->Add(std::move(row));
		}
	}
	return grid;
}

static constexpr std::initializer_list<NWidgetPart> _nested_build_palette_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_BP_CAPTION), SetStringTip(STR_MOBILE_PALETTE_RAIL), SetTextStyle(TextColour::White),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen),
		NWidgetFunction(MakeToolGrid),
	EndContainer(),
};

static WindowDesc _build_palette_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobilePalette, WindowClass::None,
	{WindowDefaultFlag::NoFocus},
	_nested_build_palette_widgets
);

} // namespace

/** Full safe width, directly above the bottom bar. */
Point GetBuildPalettePosition([[maybe_unused]] int width, int height)
{
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	int bottom = _screen.height - safe.bottom;
	if (const Window *bar = FindWindowById(WindowClass::MobileBar, 0); bar != nullptr) {
		bottom = bar->top;
	} else if (const Window *status = FindWindowById(WindowClass::Statusbar, 0); status != nullptr) {
		bottom = status->top;
	}
	return {safe.left, bottom - height};
}

static void OpenPaletteWindow()
{
	_palette_columns = ColumnsForScreen();
	new BuildPaletteWindow(_build_palette_desc);
}

void RepositionBuildPalette()
{
	ParkToolbar(GetRailToolbar());
	Window *w = FindWindowById(WindowClass::MobilePalette, 0);
	if (w == nullptr) return;
	if (auto *bp = dynamic_cast<BuildPaletteWindow *>(w); bp != nullptr && bp->columns != ColumnsForScreen()) {
		/* Rotated: rebuild with the column count for the new width. */
		bp->keep_toolbar = true;
		bp->Close();
		OpenPaletteWindow();
		return;
	}
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	int area_width = _screen.width - safe.left - safe.right;
	if (w->width != area_width) ResizeWindow(w, area_width - w->width, 0, false);
	Point pt = GetBuildPalettePosition(w->width, w->height);
	w->SetDirty();
	w->left = pt.x;
	w->top = pt.y;
	w->SetDirty();
}

void ShowRailPalette()
{
	CloseWindowById(WindowClass::MobilePalette, 0);
	Window *rt = ShowBuildRailToolbar(_last_built_railtype);
	if (rt == nullptr) return;
	ParkToolbar(rt);
	OpenPaletteWindow();
}
