/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file problems_gui.cpp Touch feedback: problems inbox and cargo-flow overlay.
 *
 * The inbox lists what needs a decision - lost or unprofitable vehicles, old vehicles, stations
 * where cargo piles up - without saying what to do about it. Tapping an entry jumps there and
 * opens its card, where the player acts. The cargo-flow toggle shows the game's own link graph
 * overlay on the main map: where goods actually go is the information for "where next".
 */

#include "../stdafx.h"
#include "problems_gui.h"
#include "card_gui.h"
#include "touch_input.h"
#include "mobile_bar_gui.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../viewport_func.h"
#include "../vehicle_base.h"
#include "../station_base.h"
#include "../company_func.h"
#include "../cargotype.h"
#include "../strings_func.h"
#include "../gfx_func.h"
#include "../zoom_func.h"
#include "../linkgraph/linkgraph_gui.h"

#include "table/strings.h"

#include "../safeguards.h"

namespace Touch {

namespace {

static constexpr uint PILING_UP_AMOUNT = 300; ///< Waiting cargo of one type that counts as piling up.
static constexpr size_t MAX_SHOWN = 6;

enum class ProblemKind : uint8_t { Lost, Unprofitable, Old, PilingUp };

struct Problem {
	ProblemKind kind;
	VehicleID vehicle = VehicleID::Invalid();
	StationID station = StationID::Invalid();
	Money money = 0;
	uint amount = 0;
};

/** Current problems, most urgent kind first. */
static std::vector<Problem> FindProblems()
{
	std::vector<Problem> found;
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle() || v->owner != _local_company) continue;
		if (v->vehicle_flags.Test(VehicleFlag::PathfinderLost)) {
			found.push_back({ProblemKind::Lost, v->index});
		} else if (v->age > TimerGameCalendar::Date{365} && v->GetDisplayProfitLastYear() < 0) {
			found.push_back({ProblemKind::Unprofitable, v->index, StationID::Invalid(), v->GetDisplayProfitLastYear()});
		} else if (v->age > v->max_age) {
			found.push_back({ProblemKind::Old, v->index});
		}
	}
	for (const Station *st : Station::Iterate()) {
		if (st->owner != _local_company) continue;
		uint most = 0;
		for (const GoodsEntry &ge : st->goods) {
			if (ge.HasData()) most = std::max(most, ge.GetData().cargo.TotalCount());
		}
		if (most >= PILING_UP_AMOUNT) found.push_back({ProblemKind::PilingUp, VehicleID::Invalid(), st->index, 0, most});
	}
	std::stable_sort(found.begin(), found.end(), [](const Problem &a, const Problem &b) { return a.kind < b.kind; });
	return found;
}

static std::string Describe(const Problem &p)
{
	switch (p.kind) {
		case ProblemKind::Lost: return GetString(STR_MOBILE_PROBLEM_LOST, p.vehicle);
		case ProblemKind::Unprofitable: return GetString(STR_MOBILE_PROBLEM_UNPROFITABLE, p.vehicle, p.money);
		case ProblemKind::Old: return GetString(STR_MOBILE_PROBLEM_OLD, p.vehicle);
		case ProblemKind::PilingUp: return GetString(STR_MOBILE_PROBLEM_PILING_UP, p.station, p.amount);
	}
	NOT_REACHED();
}

enum ProblemsWidgets : WidgetID {
	WID_PR_CAPTION,
	WID_PR_ENTRY_FIRST,
};

static std::unique_ptr<NWidgetBase> MakeEntries()
{
	auto col = std::make_unique<NWidgetVertical>();
	for (size_t i = 0; i < MAX_SHOWN; i++) {
		auto b = std::make_unique<NWidgetLeaf>(WWT_TEXTBTN, Colours::Grey, WID_PR_ENTRY_FIRST + static_cast<int>(i), WidgetData{.string = STR_EMPTY}, STR_NULL);
		b->SetFill(1, 0);
		b->SetResize(1, 0);
		col->Add(std::move(b));
	}
	return col;
}

struct ProblemsWindow : Window {
	std::vector<Problem> problems;

	ProblemsWindow(WindowDesc &desc) : Window(desc), problems(FindProblems())
	{
		this->InitNested(0);
		const SafeInsets &safe = GetSafeInsets();
		int area_width = _screen.width - safe.left - safe.right;
		if (this->width < area_width) ResizeWindow(this, area_width - this->width, 0, false);
		Point pt = GetSheetPosition(this->width, this->height);
		this->left = pt.x;
		this->top = pt.y;
		this->Refresh();
	}

	void Refresh()
	{
		for (size_t i = 0; i < MAX_SHOWN; i++) {
			this->SetWidgetDisabledState(WID_PR_ENTRY_FIRST + static_cast<int>(i), i >= this->problems.size());
		}
		this->SetDirty();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_PR_CAPTION) return GetString(this->problems.empty() ? STR_MOBILE_PROBLEMS_NONE : STR_MOBILE_PROBLEMS_CAPTION, this->problems.size());
		if (widget >= WID_PR_ENTRY_FIRST) {
			size_t i = widget - WID_PR_ENTRY_FIRST;
			return i < this->problems.size() ? Describe(this->problems[i]) : std::string{};
		}
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget >= WID_PR_ENTRY_FIRST) {
			size.height = std::max<uint>(size.height, TouchTargetPx());
			size.width = std::max<uint>(size.width, ScaleGUITrad(200));
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget < WID_PR_ENTRY_FIRST) return;
		size_t i = widget - WID_PR_ENTRY_FIRST;
		if (i >= this->problems.size()) return;
		const Problem p = this->problems[i];
		this->Close();
		if (p.vehicle != VehicleID::Invalid() && Vehicle::IsValidID(p.vehicle)) {
			const Vehicle *v = Vehicle::Get(p.vehicle);
			ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos);
			ShowVehicleCard(p.vehicle);
		} else if (p.station != StationID::Invalid() && Station::IsValidID(p.station)) {
			ScrollMainWindowToTile(Station::Get(p.station)->xy);
			ShowStationCard(p.station);
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_problems_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_PR_CAPTION), SetResize(1, 0),
	EndContainer(),
	NWidgetFunction(MakeEntries),
};

static WindowDesc _problems_desc(
	WindowPosition::Manual, {}, 0, 0,
	WindowClass::MobileProblems, WindowClass::None,
	{},
	_nested_problems_widgets
);

} // namespace

size_t CountProblems()
{
	return FindProblems().size();
}

void ShowProblems()
{
	CloseWindowById(WindowClass::MobileProblems, 0);
	new ProblemsWindow(_problems_desc);
}

bool IsCargoFlowShown()
{
	const Window *w = GetMainWindow();
	return w != nullptr && w->viewport != nullptr && w->viewport->overlay != nullptr && w->viewport->overlay->GetCargoMask().Any();
}

void ToggleCargoFlow()
{
	Window *w = GetMainWindow();
	if (w == nullptr || w->viewport == nullptr || w->viewport->overlay == nullptr) return;
	if (IsCargoFlowShown()) {
		w->viewport->overlay->SetCargoMask({});
	} else {
		CargoTypes all{};
		for (const CargoSpec *cs : CargoSpec::Iterate()) all.Set(cs->Index());
		CompanyMask own{};
		own.Set(_local_company);
		w->viewport->overlay->SetCompanyMask(own);
		w->viewport->overlay->SetCargoMask(all);
	}
	w->SetDirty();
}

} // namespace Touch
