/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file touch_input.h Touch gesture recognition for mobile platforms (Android, Web). */

#ifndef MOBILE_TOUCH_INPUT_H
#define MOBILE_TOUCH_INPUT_H

#include <cstdint>

namespace Touch {

/** Raw finger event, in screen pixels; independent of the video driver. */
struct FingerEvent {
	enum class Type : uint8_t { Down, Motion, Up, Cancel };
	Type type;
	uint64_t finger_id;
	float x; ///< Screen pixels.
	float y; ///< Screen pixels.
};

void Init(float density);
float GetDensity();
int GetAutomaticScale();
void HandleFinger(const FingerEvent &ev);
void Update();

} // namespace Touch

#endif /* MOBILE_TOUCH_INPUT_H */
