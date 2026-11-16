/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file lifecycle.h Phone app lifecycle: save on background, resume on start. */

#ifndef MOBILE_LIFECYCLE_H
#define MOBILE_LIFECYCLE_H

namespace Touch {

/** Save the running game for resuming (app goes to the background). Call with the game state locked. */
void SaveForResume();
/** On a touch device, load the resume save instead of showing the main menu. Call before the main loop. */
void TryResume();
/** The player left the game deliberately: start at the menu next time. */
void ForgetResume();

} // namespace Touch

#endif /* MOBILE_LIFECYCLE_H */
