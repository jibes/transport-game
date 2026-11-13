/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file touch_input.cpp Touch gesture recognition for mobile platforms.
 *
 * Gestures are translated into the existing mouse input path, so every upstream window works:
 *  - tap              -> left click
 *  - long-press       -> right click (tooltips, remove mode)
 *  - one-finger drag  -> left-button drag (map scroll with ViewportScrollMode::MapLMB, or the active tool)
 *  - two-finger drag  -> scroll main viewport
 *  - pinch            -> zoom main viewport, stepwise with hysteresis
 *  - release of a map scroll keeps the map moving with exponentially decaying velocity (momentum)
 */

#include "../stdafx.h"
#include "touch_input.h"
#include "../gfx_func.h"
#include "../window_func.h"
#include "../window_gui.h"
#include "../viewport_func.h"
#include "../zoom_func.h"
#include "../settings_type.h"
#include "../vehicle_type.h"
#include "mobile_bar_gui.h"

#include <chrono>
#include <cmath>
#include <vector>

#include "../safeguards.h"

namespace Touch {

using Clock = std::chrono::steady_clock;

static constexpr float SLOP_DP = 8.0f; ///< Movement below this is still a tap / long-press.
static constexpr auto LONG_PRESS = std::chrono::milliseconds(450);
static constexpr float PINCH_STEP = 1.6f; ///< Finger distance ratio per zoom level (hysteresis vs. 2x zoom).
static constexpr float FLING_TAU_MS = 325.0f; ///< Momentum decay time constant (as Android's scroller).
static constexpr float FLING_MIN_DP_PER_MS = 0.05f; ///< Slower releases and residual motion stop dead.
static constexpr float VELOCITY_SMOOTHING_MS = 50.0f; ///< Time constant of the velocity estimate.

struct Finger {
	uint64_t id;
	float x, y;
	float start_x, start_y;
};

enum class State : uint8_t {
	Idle,
	Pending, ///< One finger down, not yet classified.
	Dragging, ///< One finger, left button held.
	LongPressed, ///< Right click delivered; ignore until lift.
	Multi, ///< Two or more fingers: pan/pinch.
	ConfirmPending, ///< Construction drag finished, left button still held until Build/Cancel.
};

static float _density = 1.0f;
static std::vector<Finger> _fingers;
static State _state = State::Idle;
static Clock::time_point _down_time;
static float _pinch_ref_dist = 0.0f;
static float _mid_x = 0.0f, _mid_y = 0.0f;
static SafeInsets _safe_insets;

static constexpr uint64_t NO_FINGER = UINT64_MAX;
static uint64_t _primary_id = NO_FINGER; ///< Finger driving the current one-finger gesture.
static uint64_t _confirm_finger = NO_FINGER; ///< Finger currently pressing a Build/Cancel button.
static ConfirmHit _confirm_hit = ConfirmHit::None;
static float _pending_x = 0.0f, _pending_y = 0.0f; ///< Where the pending construction drag ended.

/** Scroll velocity estimate (screen px per ms, direction of finger travel) and momentum state. */
static float _vel_x = 0.0f, _vel_y = 0.0f;
static Clock::time_point _last_move_time;
static float _last_track_x = 0.0f, _last_track_y = 0.0f;
static bool _flinging = false;
static bool _drag_on_main_view = false; ///< The current one-finger drag started on the main map, not a window.
static Clock::time_point _fling_time;
static float _fling_rem_x = 0.0f, _fling_rem_y = 0.0f; ///< Sub-pixel remainder of momentum scrolling.

void Init(float density)
{
	_density = density > 0.0f ? density : 1.0f;
}

/**
 * Touch has no right button to hold, so dragging the map with one finger must be the left button.
 * Applied on first touch rather than in Init: the full config is loaded after the video driver
 * starts and would overwrite it; this also leaves mouse users on the Web build untouched.
 */
static bool _touch_used = false;

static void ApplyTouchDefaults()
{
	_settings_client.gui.scroll_mode = ViewportScrollMode::MapLMB;
	if (!_touch_used) {
		_touch_used = true;
		ShowMobileBar(); // no-op outside a running game; later games show it from ShowVitalWindows()
	}
}

bool IsTouchUsed()
{
	return _touch_used;
}

/** Minimum height of list rows (dropdowns) for touch; 28 GUI units is 28dp at the mobile GUI scale. */
uint32_t GetMinItemHeight()
{
	return _touch_used ? ScaleGUITrad(28) : 0;
}

void SetSafeInsets(const SafeInsets &insets)
{
	_safe_insets = insets;
}

const SafeInsets &GetSafeInsets()
{
	return _safe_insets;
}

float GetDensity()
{
	return _density;
}

/** One GUI pixel per density-independent pixel: the scale Android itself uses for UI. */
int GetAutomaticScale()
{
	return static_cast<int>(_density * 100.0f) / 25 * 25;
}

static Finger *FindFinger(uint64_t id)
{
	for (Finger &f : _fingers) {
		if (f.id == id) return &f;
	}
	return nullptr;
}

static void MoveCursor(float x, float y)
{
	_cursor.in_window = true;
	_cursor.UpdateCursorPosition(static_cast<int>(x), static_cast<int>(y));
	HandleMouseEvents();
}

static void Press(ControlInput button)
{
	_input_pressed.Set(button);
	if (button != ControlInput::Left) _input_clicked.Set(button);
	HandleMouseEvents();
}

static void Release(ControlInput button)
{
	_input_pressed.Reset(button);
	if (button == ControlInput::Left) _input_clicked.Reset(button);
	HandleMouseEvents();
}

static void Click(ControlInput button, float x, float y)
{
	MoveCursor(x, y);
	Press(button);
	Release(button);
}

/** Hide the software cursor after a gesture; touch has no hover. */
static void HideCursor()
{
	UndrawMouseCursor();
	_cursor.in_window = false;
}

/** Start velocity tracking at a finger position. */
static void TrackStart(float x, float y)
{
	_vel_x = _vel_y = 0.0f;
	_last_track_x = x;
	_last_track_y = y;
	_last_move_time = Clock::now();
}

/** Feed a new position into the smoothed velocity estimate. */
static void Track(float x, float y)
{
	auto now = Clock::now();
	float dt = std::chrono::duration<float, std::milli>(now - _last_move_time).count();
	if (dt <= 0.0f) return;
	float alpha = 1.0f - std::exp(-dt / VELOCITY_SMOOTHING_MS);
	_vel_x += alpha * ((x - _last_track_x) / dt - _vel_x);
	_vel_y += alpha * ((y - _last_track_y) / dt - _vel_y);
	_last_track_x = x;
	_last_track_y = y;
	_last_move_time = now;
}

/** Move the main viewport so its content follows a finger displacement of (dx, dy) screen pixels. */
static void ScrollMainViewport(int dx, int dy)
{
	Window *w = GetMainWindow();
	if (w == nullptr || w->viewport == nullptr || (dx == 0 && dy == 0)) return;
	w->viewport->follow_vehicle = VehicleID::Invalid();
	w->viewport->dest_scrollpos_x -= ScaleByZoom(dx, w->viewport->zoom);
	w->viewport->dest_scrollpos_y -= ScaleByZoom(dy, w->viewport->zoom);
}

/** Begin momentum scrolling if the finger was still moving when lifted. */
static void StartFling()
{
	/* A finger that paused before lifting has no momentum, whatever the old estimate says. */
	float idle = std::chrono::duration<float, std::milli>(Clock::now() - _last_move_time).count();
	if (idle > 3 * VELOCITY_SMOOTHING_MS) return;
	if (std::hypot(_vel_x, _vel_y) < FLING_MIN_DP_PER_MS * _density * 4) return;
	_flinging = true;
	_fling_time = Clock::now();
	_fling_rem_x = _fling_rem_y = 0.0f;
}

static float Distance(const Finger &a, const Finger &b)
{
	return std::hypot(a.x - b.x, a.y - b.y);
}

static void BeginMulti()
{
	if (_state == State::Dragging) Release(ControlInput::Left);
	_state = State::Multi;
	const Finger &a = _fingers[0], &b = _fingers[1];
	_pinch_ref_dist = std::max(1.0f, Distance(a, b));
	_mid_x = (a.x + b.x) / 2;
	_mid_y = (a.y + b.y) / 2;
	TrackStart(_mid_x, _mid_y);
}

static void UpdateMulti()
{
	const Finger &a = _fingers[0], &b = _fingers[1];
	float mid_x = (a.x + b.x) / 2;
	float mid_y = (a.y + b.y) / 2;

	Track(mid_x, mid_y);
	Window *w = GetMainWindow();
	if (w != nullptr && w->viewport != nullptr) {
		/* Pan: the map follows the fingers. */
		int dx = static_cast<int>(mid_x - _mid_x);
		int dy = static_cast<int>(mid_y - _mid_y);
		ScrollMainViewport(dx, dy);
		_mid_x += dx;
		_mid_y += dy;

		/* Pinch: one zoom level per PINCH_STEP change in finger distance, centred on the fingers. */
		float ratio = Distance(a, b) / _pinch_ref_dist;
		if (ratio > PINCH_STEP || ratio < 1.0f / PINCH_STEP) {
			bool zoom_in = ratio > 1.0f;
			_cursor.UpdateCursorPosition(static_cast<int>(mid_x), static_cast<int>(mid_y));
			ZoomInOrOutToCursorWindow(zoom_in, w);
			_pinch_ref_dist = Distance(a, b);
		}
	}
}

/** A construction tool is mid-drag (rail line, road, area clear, ...), as opposed to scrolling. */
static bool IsToolDrag()
{
	return _special_mouse_mode == SpecialMouseMode::Sizing;
}

/** Finish a pending construction drag: build it where it ended, or drop it and keep the tool. */
static void ResolveConfirm(bool build)
{
	CloseBuildConfirm();
	if (build) {
		MoveCursor(_pending_x, _pending_y);
	} else {
		VpCancelPlaceSizingDrag();
	}
	Release(ControlInput::Left);
	_state = State::Idle;
	_confirm_finger = NO_FINGER;
	HideCursor();
}

/** First finger of a new gesture. */
static void BeginGesture(const FingerEvent &ev)
{
	_fingers.push_back({ev.finger_id, ev.x, ev.y, ev.x, ev.y});
	_primary_id = ev.finger_id;
	TrackStart(ev.x, ev.y);
	_state = State::Pending;
	_down_time = Clock::now();
	MoveCursor(ev.x, ev.y);
}

static void HandleDown(const FingerEvent &ev)
{
	ApplyTouchDefaults();
	_flinging = false; // touching the map stops momentum, as on any phone

	if (_state == State::ConfirmPending) {
		if (_confirm_finger != NO_FINGER) return; // one button press at a time
		ConfirmHit hit = GetBuildConfirmHit(static_cast<int>(ev.x), static_cast<int>(ev.y));
		if (hit != ConfirmHit::None) {
			_confirm_finger = ev.finger_id;
			_confirm_hit = hit;
			return;
		}
		if (FindWindowFromPt(static_cast<int>(ev.x), static_cast<int>(ev.y)) == GetMainWindow()) {
			/* Touching the map again moves the end point of the pending selection. */
			CloseBuildConfirm();
			_fingers.push_back({ev.finger_id, ev.x, ev.y, ev.x, ev.y});
			_primary_id = ev.finger_id;
			_state = State::Dragging;
			MoveCursor(ev.x, ev.y);
			return;
		}
		/* Touching any other window abandons the pending build, then acts normally. */
		ResolveConfirm(false);
	}

	/* Extra fingers during a construction drag would end it (and build); ignore them entirely. */
	if (_state == State::Dragging && IsToolDrag()) return;

	if (_fingers.empty()) {
		BeginGesture(ev);
		return;
	}
	_fingers.push_back({ev.finger_id, ev.x, ev.y, ev.x, ev.y});
	if (_fingers.size() == 2) BeginMulti();
}

static void HandleMotion(const FingerEvent &ev)
{
	Finger *f = FindFinger(ev.finger_id);
	if (f == nullptr) return;
	f->x = ev.x;
	f->y = ev.y;
	switch (_state) {
		case State::Pending:
			if (std::hypot(f->x - f->start_x, f->y - f->start_y) > SLOP_DP * _density) {
				/* Press at the start point so drags begin where the finger landed. */
				MoveCursor(f->start_x, f->start_y);
				_drag_on_main_view = FindWindowFromPt(static_cast<int>(f->start_x), static_cast<int>(f->start_y)) == GetMainWindow();
				Press(ControlInput::Left);
				_state = State::Dragging;
				MoveCursor(f->x, f->y);
			}
			break;
		case State::Dragging:
			if (f->id != _primary_id) break;
			Track(f->x, f->y);
			MoveCursor(f->x, f->y);
			break;
		case State::Multi:
			if (_fingers.size() >= 2) UpdateMulti();
			break;
		default:
			break;
	}
}

static void HandleUp(const FingerEvent &ev)
{
	bool cancelled = ev.type == FingerEvent::Type::Cancel;

	if (ev.finger_id == _confirm_finger) {
		/* A button counts only if the finger is still on it when lifted. */
		ConfirmHit hit = GetBuildConfirmHit(static_cast<int>(ev.x), static_cast<int>(ev.y));
		_confirm_finger = NO_FINGER;
		if (!cancelled && hit == _confirm_hit) ResolveConfirm(hit == ConfirmHit::Build);
		return;
	}

	Finger *f = FindFinger(ev.finger_id);
	if (f == nullptr) return;
	float x = f->x, y = f->y;
	_fingers.erase(_fingers.begin() + (f - _fingers.data()));
	if (!_fingers.empty()) return; // lifting one of several fingers keeps the gesture until all are up

	if (_state == State::Dragging && IsToolDrag()) {
		if (cancelled) {
			VpCancelPlaceSizingDrag();
			Release(ControlInput::Left);
		} else {
			/* Hold the selection; Build/Cancel decides. */
			_pending_x = x;
			_pending_y = y;
			_state = State::ConfirmPending;
			ShowBuildConfirm(static_cast<int>(x), static_cast<int>(y));
			/* Cursor stays "in window": the tile selection (and its highlight) tracks the cursor and
			 * would collapse otherwise. The tool cursor also marks the end point. */
			return;
		}
	} else {
		/* Momentum only for map scrolls of the main view, not for tool drags. */
		bool map_scroll = (_state == State::Dragging && _scrolling_viewport && _drag_on_main_view) || _state == State::Multi;
		if (!cancelled) {
			if (_state == State::Pending) Click(ControlInput::Left, x, y);
			if (_state == State::Dragging) Release(ControlInput::Left);
			if (map_scroll) StartFling();
		} else if (_state == State::Dragging) {
			Release(ControlInput::Left);
		}
	}
	_state = State::Idle;
	_primary_id = NO_FINGER;
	HideCursor();
}

void HandleFinger(const FingerEvent &ev)
{
	switch (ev.type) {
		case FingerEvent::Type::Down: HandleDown(ev); break;
		case FingerEvent::Type::Motion: HandleMotion(ev); break;
		case FingerEvent::Type::Up:
		case FingerEvent::Type::Cancel: HandleUp(ev); break;
	}
}

/** Advance momentum scrolling. */
static void UpdateFling()
{
	if (!_flinging) return;
	auto now = Clock::now();
	float dt = std::chrono::duration<float, std::milli>(now - _fling_time).count();
	_fling_time = now;
	if (dt <= 0.0f) return;

	float decay = std::exp(-dt / FLING_TAU_MS);
	_vel_x *= decay;
	_vel_y *= decay;
	if (std::hypot(_vel_x, _vel_y) < FLING_MIN_DP_PER_MS * _density) {
		_flinging = false;
		return;
	}

	_fling_rem_x += _vel_x * dt;
	_fling_rem_y += _vel_y * dt;
	int dx = static_cast<int>(_fling_rem_x);
	int dy = static_cast<int>(_fling_rem_y);
	_fling_rem_x -= dx;
	_fling_rem_y -= dy;
	ScrollMainViewport(dx, dy);
}

/** Called every input poll: momentum, and the long-press, which has no event of its own. */
void Update()
{
	UpdateFling();
	if (_state == State::ConfirmPending && !IsToolDrag()) {
		/* The tool was closed (e.g. its toolbar) while a build was pending: nothing left to confirm. */
		CloseBuildConfirm();
		_input_pressed.Reset(ControlInput::Left);
		_input_clicked.Reset(ControlInput::Left);
		_state = State::Idle;
		_confirm_finger = NO_FINGER;
	}
	if (_state != State::Pending || _fingers.size() != 1) return;
	if (Clock::now() - _down_time < LONG_PRESS) return;
	const Finger &f = _fingers[0];
	Click(ControlInput::Right, f.x, f.y);
	_state = State::LongPressed;
}

} // namespace Touch
