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
#include "../widgets/road_widget.h"
#include "../widgets/dock_widget.h"
#include "../widgets/airport_widget.h"
#include "../widgets/terraform_widget.h"
#include "../road_gui.h"
#include "../road_type.h"
#include "../terraform_gui.h"
#include "../gui.h"
#include "../zoom_func.h"
#include "../core/math_func.hpp"
#include "../table/sprites.h"

#include "table/strings.h"

#include "../safeguards.h"

extern RailType _last_built_railtype;
extern RoadType _last_built_roadtype;
extern RoadType _last_built_tramtype;

namespace {

enum BuildPaletteWidgets : WidgetID {
	WID_BP_CLOSE,
	WID_BP_TOOL_FIRST, ///< First tool button; tools follow in the order of the current mode's tool table.
};

struct PaletteTool {
	WidgetID toolbar_widget; ///< Widget of the construction toolbar this button forwards to.
	StringID label;
};

/** Most used tools per mode; rarer ones (fixed-direction pieces, conversion) stay in the full toolbar. */
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

static constexpr PaletteTool ROAD_TOOLS[] = {
	{WID_ROT_AUTOROAD, STR_MOBILE_TOOL_ROAD},
	{WID_ROT_BUS_STATION, STR_MOBILE_TOOL_BUS_STOP},
	{WID_ROT_TRUCK_STATION, STR_MOBILE_TOOL_LORRY_STOP},
	{WID_ROT_DEPOT, STR_MOBILE_TOOL_DEPOT},
	{WID_ROT_ONE_WAY, STR_MOBILE_TOOL_ONE_WAY},
	{WID_ROT_BUILD_BRIDGE, STR_MOBILE_TOOL_BRIDGE},
	{WID_ROT_BUILD_TUNNEL, STR_MOBILE_TOOL_TUNNEL},
	{WID_ROT_DEMOLISH, STR_MOBILE_TOOL_DEMOLISH},
	{WID_ROT_REMOVE, STR_MOBILE_TOOL_REMOVE},
};

static constexpr PaletteTool TRAM_TOOLS[] = {
	{WID_ROT_AUTOROAD, STR_MOBILE_TOOL_TRAMWAY},
	{WID_ROT_BUS_STATION, STR_MOBILE_TOOL_PASSENGER_STOP},
	{WID_ROT_TRUCK_STATION, STR_MOBILE_TOOL_FREIGHT_STOP},
	{WID_ROT_DEPOT, STR_MOBILE_TOOL_DEPOT},
	{WID_ROT_BUILD_BRIDGE, STR_MOBILE_TOOL_BRIDGE},
	{WID_ROT_BUILD_TUNNEL, STR_MOBILE_TOOL_TUNNEL},
	{WID_ROT_DEMOLISH, STR_MOBILE_TOOL_DEMOLISH},
	{WID_ROT_REMOVE, STR_MOBILE_TOOL_REMOVE},
};

static constexpr PaletteTool WATER_TOOLS[] = {
	{WID_DT_CANAL, STR_MOBILE_TOOL_CANAL},
	{WID_DT_LOCK, STR_MOBILE_TOOL_LOCK},
	{WID_DT_STATION, STR_MOBILE_TOOL_DOCK},
	{WID_DT_DEPOT, STR_MOBILE_TOOL_DEPOT},
	{WID_DT_BUOY, STR_MOBILE_TOOL_BUOY},
	{WID_DT_BUILD_AQUEDUCT, STR_MOBILE_TOOL_AQUEDUCT},
	{WID_DT_DEMOLISH, STR_MOBILE_TOOL_DEMOLISH},
};

static constexpr PaletteTool AIR_TOOLS[] = {
	{WID_AT_AIRPORT, STR_MOBILE_TOOL_AIRPORT},
	{WID_AT_DEMOLISH, STR_MOBILE_TOOL_DEMOLISH},
};

static constexpr PaletteTool LANDSCAPE_TOOLS[] = {
	{WID_TT_LOWER_LAND, STR_MOBILE_TOOL_LOWER},
	{WID_TT_RAISE_LAND, STR_MOBILE_TOOL_RAISE},
	{WID_TT_LEVEL_LAND, STR_MOBILE_TOOL_LEVEL},
	{WID_TT_DEMOLISH, STR_MOBILE_TOOL_DEMOLISH},
	{WID_TT_BUY_LAND, STR_MOBILE_TOOL_BUY_LAND},
	{WID_TT_PLANT_TREES, STR_MOBILE_TOOL_TREES},
	{WID_TT_PLACE_SIGN, STR_MOBILE_TOOL_SIGN},
	{WID_TT_PLACE_OBJECT, STR_MOBILE_TOOL_OBJECT},
};

/** The build-mode chooser (Build button): one cell per mode; toolbar_widget holds the PaletteMode. */
static constexpr PaletteTool CHOOSER_TOOLS[] = {
	{to_underlying(PaletteMode::Rail), STR_MOBILE_BUILD_RAIL},
	{to_underlying(PaletteMode::Road), STR_MOBILE_BUILD_ROAD},
	{to_underlying(PaletteMode::Tram), STR_MOBILE_BUILD_TRAM},
	{to_underlying(PaletteMode::Water), STR_MOBILE_BUILD_WATER},
	{to_underlying(PaletteMode::Air), STR_MOBILE_BUILD_AIR},
	{to_underlying(PaletteMode::Landscape), STR_MOBILE_BUILD_LANDSCAPE},
};
static constexpr SpriteID CHOOSER_SPRITES[] = {
	SPR_IMG_BUILDRAIL, SPR_IMG_BUILDROAD, SPR_IMG_BUILDTRAMS, SPR_IMG_BUILDWATER, SPR_IMG_BUILDAIR, SPR_IMG_LANDSCAPING,
};
static_assert(std::size(CHOOSER_TOOLS) == std::size(CHOOSER_SPRITES));

/** A build mode: which (parked) construction toolbar the palette drives, and with which tools. */
struct PaletteKind {
	WindowClass toolbar_class;
	int toolbar_number;
	StringID caption;
	std::span<const PaletteTool> tools;
	Window *(*open)(); ///< Opens the construction toolbar.
};

static const PaletteKind PALETTE_KINDS[] = {
	{WindowClass::BuildToolbar, to_underlying(TransportType::Rail), STR_MOBILE_BUILD_RAIL, RAIL_TOOLS, [] { return ShowBuildRailToolbar(_last_built_railtype); }},
	{WindowClass::BuildToolbar, to_underlying(TransportType::Road), STR_MOBILE_BUILD_ROAD, ROAD_TOOLS, [] { return ShowBuildRoadToolbar(_last_built_roadtype); }},
	{WindowClass::BuildToolbar, to_underlying(TransportType::Road), STR_MOBILE_BUILD_TRAM, TRAM_TOOLS, [] { return ShowBuildRoadToolbar(_last_built_tramtype); }},
	{WindowClass::BuildToolbar, to_underlying(TransportType::Water), STR_MOBILE_BUILD_WATER, WATER_TOOLS, [] { return ShowBuildDocksToolbar(); }},
	{WindowClass::BuildToolbar, to_underlying(TransportType::Air), STR_MOBILE_BUILD_AIR, AIR_TOOLS, [] { return ShowBuildAirToolbar(); }},
	{WindowClass::ScenarioGenerateLandscape, 0, STR_MOBILE_BUILD_LANDSCAPE, LANDSCAPE_TOOLS, [] { return ShowTerraformToolbar(nullptr); }},
	{WindowClass::Invalid, 0, STR_MOBILE_BAR_BUILD, CHOOSER_TOOLS, nullptr},
};
static_assert(std::size(PALETTE_KINDS) == static_cast<size_t>(PaletteMode::End));

static PaletteMode _palette_mode = PaletteMode::Rail; ///< Mode of the palette being built / shown.

static const PaletteKind &CurrentKind()
{
	return PALETTE_KINDS[to_underlying(_palette_mode)];
}

/** The chooser drives no toolbar; its cells open the mode palettes. */
static bool IsChooser()
{
	return _palette_mode == PaletteMode::Chooser;
}

static int NumTools()
{
	return static_cast<int>(CurrentKind().tools.size());
}

static bool _palette_open = false; ///< A palette occupies the bottom action row.
static int _palette_columns = 5; ///< Columns of the palette being built (MakeToolGrid reads it).

/** Cells: close button plus tools. */
static int NumCells()
{
	return NumTools() + 1;
}

/**
 * Fewest rows of finger-sized icons that fit the safe width, then the columns spread evenly
 * over those rows (e.g. 10 cells in portrait: 2 rows of 5, not 8 + 2).
 */
static int ColumnsForScreen()
{
	const Touch::SafeInsets &safe = Touch::GetSafeInsets();
	int area_width = _screen.width - safe.left - safe.right;
	int per_row = std::max(1, area_width / Touch::TouchTargetPx());
	int rows = CeilDiv(NumCells(), per_row);
	return CeilDiv(NumCells(), rows);
}

/** The parked construction toolbar the palette drives, if still open. */
static Window *GetToolbar()
{
	const PaletteKind &kind = CurrentKind();
	return FindWindowById(kind.toolbar_class, kind.toolbar_number);
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
		if (IsChooser()) {
			for (int i = 0; i < NumTools(); i++) this->GetWidget<NWidgetCore>(WID_BP_TOOL_FIRST + i)->SetSprite(CHOOSER_SPRITES[i]);
		} else if (const Window *rt = GetToolbar(); rt != nullptr) {
			for (int i = 0; i < NumTools(); i++) {
				const NWidgetCore *src = rt->GetWidget<NWidgetCore>(CurrentKind().tools[i].toolbar_widget);
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

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget < WID_BP_CLOSE || widget >= WID_BP_TOOL_FIRST + NumTools()) return;
		int t = Touch::TouchTargetPx();
		size.width = std::max<uint>(size.width, t);
		size.height = std::max<uint>(size.height, t);
	}

	void Close(int data = 0) override
	{
		if (!this->keep_toolbar) {
			const PaletteKind &kind = CurrentKind();
			if (!IsChooser()) CloseWindowById(kind.toolbar_class, kind.toolbar_number);
			_palette_open = false;
			RepositionMobileBar(); // bring the navigation back into the bottom row
		}
		this->Window::Close(data);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, int click_count) override
	{
		if (widget < WID_BP_TOOL_FIRST || widget >= WID_BP_TOOL_FIRST + NumTools()) return;
		if (IsChooser()) {
			ShowBuildPalette(static_cast<PaletteMode>(CurrentKind().tools[widget - WID_BP_TOOL_FIRST].toolbar_widget));
			return;
		}
		Window *rt = GetToolbar();
		if (rt == nullptr) return;
		rt->OnClick({0, 0}, CurrentKind().tools[widget - WID_BP_TOOL_FIRST].toolbar_widget, click_count);
		this->SyncLowered();
	}

	/** Mirror the toolbar's pressed tool, and follow it when it closes. */
	void OnMouseLoop() override
	{
		if (IsChooser()) return;
		const Window *rt = GetToolbar();
		if (rt == nullptr) {
			this->Close();
			return;
		}
		ParkToolbar(const_cast<Window *>(rt));
		this->SyncLowered();
	}

	void SyncLowered()
	{
		if (IsChooser()) return;
		const Window *rt = GetToolbar();
		if (rt == nullptr) return;
		for (int i = 0; i < NumTools(); i++) {
			bool lowered = rt->IsWidgetLowered(CurrentKind().tools[i].toolbar_widget);
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
	for (int i = 0; i < NumCells(); i++) {
		if (i % _palette_columns == 0) row = std::make_unique<NWidgetHorizontal>(NWidContainerFlag::EqualSize);
		std::unique_ptr<NWidgetLeaf> cell;
		if (i == 0) {
			/* Close is a cell like any tool: same size, first position. */
			cell = std::make_unique<NWidgetLeaf>(WWT_CLOSEBOX, Colours::DarkGreen, WID_BP_CLOSE, WidgetData{}, STR_TOOLTIP_CLOSE_WINDOW);
		} else {
			/* Icon only; the name is the tooltip (long-press). Sprite is copied from the toolbar. */
			cell = std::make_unique<NWidgetLeaf>(WWT_IMGBTN, Colours::DarkGreen, WID_BP_TOOL_FIRST + i - 1, WidgetData{}, CurrentKind().tools[i - 1].label);
		}
		cell->SetFill(1, 1);
		cell->SetResize(1, 0);
		row->Add(std::move(cell));
		if (i % _palette_columns == _palette_columns - 1) {
			grid->Add(std::move(row));
		} else if (i == NumCells() - 1) {
			for (int pad = i % _palette_columns + 1; pad < _palette_columns; pad++) {
				auto spacer = std::make_unique<NWidgetSpacer>(0, 0);
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

/** Full safe width, in the bottom action row (the navigation steps aside while a palette is open). */
Point GetBuildPalettePosition([[maybe_unused]] int width, int height)
{
	return {Touch::GetSafeInsets().left, GetActionRowBottom() - height};
}

bool IsBuildPaletteOpen()
{
	return _palette_open;
}

static void OpenPaletteWindow()
{
	_palette_columns = ColumnsForScreen();
	_palette_open = true;
	RepositionMobileBar(); // park the navigation; the palette takes its row
	new BuildPaletteWindow(_build_palette_desc);
}

void RepositionBuildPalette()
{
	ParkToolbar(GetToolbar());
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

void ShowBuildPalette(PaletteMode mode)
{
	CloseWindowById(WindowClass::MobilePalette, 0);
	_palette_mode = mode;
	if (CurrentKind().open != nullptr) {
		Window *rt = CurrentKind().open();
		if (rt == nullptr) return;
		ParkToolbar(rt);
	}
	OpenPaletteWindow();
}
