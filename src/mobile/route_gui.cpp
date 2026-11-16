/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file route_gui.cpp Touch "Route" tool.
 *
 * Tap the start, tap the end; the sheet then compares up to three routes (cheap / balanced /
 * direct) by cost, length and curves, the map shows the selected one, and Build places it with
 * the normal BuildRail command, piece by piece. The player makes the trade-off; the game does the
 * tile-by-tile placement that is tedious with a finger.
 */

#include "../stdafx.h"
#include "route_gui.h"
#include "route_planner.h"
#include "touch_input.h"
#include "mobile_bar_gui.h"
#include "build_palette_gui.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../viewport_func.h"
#include "../command_func.h"
#include "../rail_cmd.h"
#include "../tile_map.h"
#include "../map_func.h"
#include "../landscape.h"
#include "../strings_func.h"
#include "../gfx_func.h"
#include "../zoom_func.h"
#include "../core/math_func.hpp"
#include "../tilehighlight_func.h"

#include "table/strings.h"

#include "../safeguards.h"

namespace Touch {

namespace {

enum class RouteStage : uint8_t { Off, PickStart, PickEnd, Choose };

struct RouteState {
	RouteStage stage = RouteStage::Off;
	RailType railtype = INVALID_RAILTYPE;
	TileIndex start = INVALID_TILE;
	TileIndex end = INVALID_TILE;
	std::vector<RoutePlan> options; ///< Distinct alternatives, in RouteStyle order.
	std::vector<RouteStyle> option_styles;
	int selected = 0;
};

static RouteState _route;

enum RouteWidgets : WidgetID {
	WID_RT_CLOSE,
	WID_RT_PROMPT,
	WID_RT_OPTION_FIRST, ///< One button per alternative (up to 3).
	WID_RT_BUILD = WID_RT_OPTION_FIRST + 3,
};

static StringID StyleName(RouteStyle s)
{
	switch (s) {
		case RouteStyle::Cheap: return STR_MOBILE_ROUTE_CHEAP;
		case RouteStyle::Balanced: return STR_MOBILE_ROUTE_BALANCED;
		case RouteStyle::Direct: return STR_MOBILE_ROUTE_DIRECT;
		default: NOT_REACHED();
	}
}

/** Whole map redraw: the overlay spans the route's area. */
static void RedrawRoute()
{
	MarkWholeScreenDirty();
}

struct RouteWindow : Window {
	RouteWindow(WindowDesc &desc) : Window(desc)
	{
		this->InitNested(0);
		const SafeInsets &safe = GetSafeInsets();
		int area_width = _screen.width - safe.left - safe.right;
		if (this->width < area_width) ResizeWindow(this, area_width - this->width, 0, false);
		RepositionBuildPalette(); // palette steps aside...
		this->Reposition();       // ...so this sheet takes the action row
		this->UpdateState();
	}

	void Reposition()
	{
		Point pt = GetSheetPosition(this->width, this->height);
		this->SetDirty();
		this->left = pt.x;
		this->top = pt.y;
		this->SetDirty();
	}

	void Close(int data = 0) override
	{
		_route = RouteState{};
		RedrawRoute();
		this->Window::Close(data);
		RepositionBuildPalette(); // the palette comes back into the action row
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		int t = TouchTargetPx();
		if (widget == WID_RT_CLOSE || widget == WID_RT_BUILD || (widget >= WID_RT_OPTION_FIRST && widget < WID_RT_BUILD)) {
			size.width = std::max<uint>(size.width, t);
			size.height = std::max<uint>(size.height, t);
		}
		if (widget >= WID_RT_OPTION_FIRST && widget < WID_RT_BUILD) {
			/* Two lines: style name, then cost/length/curves. */
			size.height = std::max<uint>(size.height, 2 * GetCharacterHeight(FontSize::Normal) + WidgetDimensions::scaled.framerect.Vertical());
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_RT_PROMPT) {
			switch (_route.stage) {
				case RouteStage::PickStart: return GetString(STR_MOBILE_ROUTE_PICK_START);
				case RouteStage::PickEnd: return GetString(STR_MOBILE_ROUTE_PICK_END);
				case RouteStage::Choose: return GetString(_route.options.empty() ? STR_MOBILE_ROUTE_NONE : STR_MOBILE_ROUTE_CHOOSE);
				default: return {};
			}
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget < WID_RT_OPTION_FIRST || widget >= WID_RT_BUILD) return;
		int i = widget - WID_RT_OPTION_FIRST;
		if (i >= static_cast<int>(_route.options.size())) return;
		const RoutePlan &p = _route.options[i];
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int line = GetCharacterHeight(FontSize::Normal);
		int top = CentreBounds(ir.top, ir.bottom, 2 * line);
		DrawString(ir.left, ir.right, top, StyleName(_route.option_styles[i]), i == _route.selected ? TextColour::White : TextColour::Black, AlignmentH::Centre);
		DrawString(ir.left, ir.right, top + line, GetString(STR_MOBILE_ROUTE_SUMMARY, p.cost, p.steps.size(), p.curves), TextColour::Black, AlignmentH::Centre, false, FontSize::Small);
	}

	void UpdateState()
	{
		bool choosing = _route.stage == RouteStage::Choose;
		for (int i = 0; i < 3; i++) {
			bool used = choosing && i < static_cast<int>(_route.options.size());
			this->SetWidgetDisabledState(WID_RT_OPTION_FIRST + i, !used);
			this->SetWidgetLoweredState(WID_RT_OPTION_FIRST + i, used && i == _route.selected);
		}
		this->SetWidgetDisabledState(WID_RT_BUILD, !choosing || _route.options.empty());
		this->SetDirty();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget >= WID_RT_OPTION_FIRST && widget < WID_RT_BUILD) {
			int i = widget - WID_RT_OPTION_FIRST;
			if (i < static_cast<int>(_route.options.size())) {
				_route.selected = i;
				this->UpdateState();
				RedrawRoute();
			}
			return;
		}
		if (widget == WID_RT_BUILD && _route.stage == RouteStage::Choose && !_route.options.empty()) {
			for (const RouteStep &s : _route.options[_route.selected].steps) {
				if (s.exists) continue;
				Command<Commands::BuildRail>::Post(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK, s.tile, _route.railtype, s.track, false);
			}
			this->Close();
		}
	}
};

static std::unique_ptr<NWidgetBase> MakeOptionRow()
{
	auto row = std::make_unique<NWidgetHorizontal>(NWidContainerFlag::EqualSize);
	for (int i = 0; i < 3; i++) {
		/* Text button with an empty label: gives the bevel and lowered state; DrawWidget adds the two lines. */
		auto b = std::make_unique<NWidgetLeaf>(WWT_TEXTBTN, Colours::Grey, WID_RT_OPTION_FIRST + i, WidgetData{.string = STR_EMPTY}, STR_NULL);
		b->SetFill(1, 1);
		b->SetResize(1, 0);
		row->Add(std::move(b));
	}
	return row;
}

static constexpr std::initializer_list<NWidgetPart> _nested_route_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen, WID_RT_CLOSE),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_RT_PROMPT), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidgetFunction(MakeOptionRow),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_RT_BUILD), SetStringTip(STR_MOBILE_CONFIRM_BUILD), SetFill(0, 1),
	EndContainer(),
};

static WindowDesc _route_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileRoute, WindowClass::None,
	{},
	_nested_route_widgets
);

static RouteWindow *GetRouteWindow()
{
	return dynamic_cast<RouteWindow *>(FindWindowById(WindowClass::MobileRoute, 0));
}

/** Plan all styles; keep distinct results. */
static void PlanOptions()
{
	_route.options.clear();
	_route.option_styles.clear();
	for (RouteStyle s : {RouteStyle::Cheap, RouteStyle::Balanced, RouteStyle::Direct}) {
		RoutePlan p = PlanRailRoute(_route.start, _route.end, _route.railtype, s);
		if (!p.found) continue;
		bool duplicate = false;
		for (const RoutePlan &o : _route.options) {
			if (o.steps.size() == p.steps.size() && std::equal(o.steps.begin(), o.steps.end(), p.steps.begin(),
					[](const RouteStep &a, const RouteStep &b) { return a.tile == b.tile && a.track == b.track; })) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) continue;
		_route.options.push_back(std::move(p));
		_route.option_styles.push_back(s);
	}
	_route.selected = 0;
}

} // namespace

void StartRoutePlanning(RailType railtype)
{
	ResetObjectToPlace(); // no other tool while routing
	CloseWindowById(WindowClass::MobileRoute, 0);
	_route = RouteState{};
	_route.stage = RouteStage::PickStart;
	_route.railtype = railtype;
	new RouteWindow(_route_desc);
}

bool IsRoutePlanning()
{
	return _route.stage == RouteStage::PickStart || _route.stage == RouteStage::PickEnd;
}

bool HandleRouteTap(const Viewport &vp, int x, int y)
{
	if (!IsRoutePlanning()) return false;
	Point pt = TranslateXYToTileCoord(vp, x, y);
	if (pt.x < 0 || pt.y < 0) return true;
	TileIndex tile = TileVirtXY(pt.x, pt.y);
	if (!IsValidTile(tile)) return true;

	if (_route.stage == RouteStage::PickStart) {
		_route.start = tile;
		_route.stage = RouteStage::PickEnd;
	} else {
		_route.end = tile;
		_route.stage = RouteStage::Choose;
		PlanOptions();
	}
	if (RouteWindow *w = GetRouteWindow(); w != nullptr) w->UpdateState();
	RedrawRoute();
	return true;
}

/**
 * Screen position (window coordinates) of the middle of a track piece. Half-tile pieces sit in a tile
 * corner (e.g. "upper" near the north corner), so a straight line through them stays straight.
 */
static Point PieceCentre(const Viewport &vp, TileIndex tile, Track track)
{
	/* Offsets in tile pixels (x towards south-west, y towards south-east). */
	static constexpr int OFFSET[][2] = {
		{8, 8},   // X
		{8, 8},   // Y
		{4, 4},   // Upper (north corner)
		{12, 12}, // Lower (south corner)
		{12, 4},  // Left (west corner)
		{4, 12},  // Right (east corner)
	};
	const auto &o = OFFSET[to_underlying(track)];
	int x = TileX(tile) * TILE_SIZE + o[0];
	int y = TileY(tile) * TILE_SIZE + o[1];
	Point p = RemapCoords(x, y, GetSlopePixelZ(x, y));
	p.x = UnScaleByZoom(p.x - vp.virtual_left, vp.zoom) + vp.left;
	p.y = UnScaleByZoom(p.y - vp.virtual_top, vp.zoom) + vp.top;
	return p;
}

void DrawRouteOverlay(const Viewport &vp, DrawPixelInfo *dpi)
{
	if (_route.stage == RouteStage::Off) return;
	AutoRestoreBackup dpi_backup(_cur_dpi, dpi);
	const int width = std::max(2, ScaleGUITrad(3));
	const PixelColour route_colour = GetColourGradient(Colours::Yellow, Shade::Lighter);
	const PixelColour mark_colour = GetColourGradient(Colours::White, Shade::Lightest);

	if (_route.stage == RouteStage::Choose && !_route.options.empty()) {
		const RoutePlan &p = _route.options[_route.selected];
		for (size_t i = 1; i < p.steps.size(); i++) {
			Point a = PieceCentre(vp, p.steps[i - 1].tile, p.steps[i - 1].track);
			Point b = PieceCentre(vp, p.steps[i].tile, p.steps[i].track);
			GfxDrawLine(a.x, a.y, b.x, b.y, route_colour, width);
		}
	}
	/* Mark the picked ends. */
	for (TileIndex t : {_route.start, _route.end}) {
		if (!IsValidTile(t)) continue;
		Point c = PieceCentre(vp, t, Track::X);
		int r = ScaleGUITrad(6);
		GfxFillRect(c.x - r, c.y - r, c.x + r, c.y + r, mark_colour);
	}
}

} // namespace Touch
