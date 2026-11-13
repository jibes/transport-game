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

#include <chrono>
#include <cmath>
#include <vector>

#include "../safeguards.h"

namespace Touch {

using Clock = std::chrono::steady_clock;

static constexpr float SLOP_DP = 8.0f; ///< Movement below this is still a tap / long-press.
static constexpr auto LONG_PRESS = std::chrono::milliseconds(450);
static constexpr float PINCH_STEP = 1.6f; ///< Finger distance ratio per zoom level (hysteresis vs. 2x zoom).

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
};

static float _density = 1.0f;
static std::vector<Finger> _fingers;
static State _state = State::Idle;
static Clock::time_point _down_time;
static float _pinch_ref_dist = 0.0f;
static float _mid_x = 0.0f, _mid_y = 0.0f;

void Init(float density)
{
	_density = density > 0.0f ? density : 1.0f;
	/* Touch has no right button to hold; dragging the map with one finger is the natural gesture. */
	_settings_client.gui.scroll_mode = ViewportScrollMode::MapLMB;
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
}

static void UpdateMulti()
{
	const Finger &a = _fingers[0], &b = _fingers[1];
	float mid_x = (a.x + b.x) / 2;
	float mid_y = (a.y + b.y) / 2;

	Window *w = GetMainWindow();
	if (w != nullptr && w->viewport != nullptr) {
		/* Pan: the map follows the fingers. */
		int dx = static_cast<int>(mid_x - _mid_x);
		int dy = static_cast<int>(mid_y - _mid_y);
		if (dx != 0 || dy != 0) {
			w->viewport->follow_vehicle = VehicleID::Invalid();
			w->viewport->dest_scrollpos_x -= ScaleByZoom(dx, w->viewport->zoom);
			w->viewport->dest_scrollpos_y -= ScaleByZoom(dy, w->viewport->zoom);
			_mid_x += dx;
			_mid_y += dy;
		}

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

void HandleFinger(const FingerEvent &ev)
{
	switch (ev.type) {
		case FingerEvent::Type::Down:
			_fingers.push_back({ev.finger_id, ev.x, ev.y, ev.x, ev.y});
			if (_fingers.size() == 1) {
				_state = State::Pending;
				_down_time = Clock::now();
				MoveCursor(ev.x, ev.y);
			} else if (_fingers.size() == 2) {
				BeginMulti();
			}
			break;

		case FingerEvent::Type::Motion: {
			Finger *f = FindFinger(ev.finger_id);
			if (f == nullptr) break;
			f->x = ev.x;
			f->y = ev.y;
			switch (_state) {
				case State::Pending:
					if (std::hypot(f->x - f->start_x, f->y - f->start_y) > SLOP_DP * _density) {
						/* Press at the start point so drags begin where the finger landed. */
						MoveCursor(f->start_x, f->start_y);
						Press(ControlInput::Left);
						_state = State::Dragging;
						MoveCursor(f->x, f->y);
					}
					break;
				case State::Dragging:
					MoveCursor(f->x, f->y);
					break;
				case State::Multi:
					if (_fingers.size() >= 2) UpdateMulti();
					break;
				default:
					break;
			}
			break;
		}

		case FingerEvent::Type::Up:
		case FingerEvent::Type::Cancel: {
			Finger *f = FindFinger(ev.finger_id);
			if (f == nullptr) break;
			float x = f->x, y = f->y;
			_fingers.erase(_fingers.begin() + (f - _fingers.data()));

			if (_fingers.empty()) {
				if (ev.type == FingerEvent::Type::Up) {
					if (_state == State::Pending) Click(ControlInput::Left, x, y);
					if (_state == State::Dragging) Release(ControlInput::Left);
				} else if (_state == State::Dragging) {
					Release(ControlInput::Left);
				}
				_state = State::Idle;
				HideCursor();
			}
			/* Lifting one of several fingers keeps the gesture in Multi until all are up. */
			break;
		}
	}
}

/** Called every input poll; fires the long-press, which has no event of its own. */
void Update()
{
	if (_state != State::Pending || _fingers.size() != 1) return;
	if (Clock::now() - _down_time < LONG_PRESS) return;
	const Finger &f = _fingers[0];
	Click(ControlInput::Right, f.x, f.y);
	_state = State::LongPressed;
}

} // namespace Touch
