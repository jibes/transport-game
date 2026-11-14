/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file mobile_bar_gui.h Bottom action bar for touch play. */

#ifndef MOBILE_BAR_GUI_H
#define MOBILE_BAR_GUI_H

#include "../core/geometry_type.hpp"

void ShowMobileBar();
bool ShowMapContextMenu(int x, int y);
void RepositionMobileBar();
Point GetMobileBarPosition(int width, int height);
int GetActionRowBottom();

/** Result of a touch on the build confirmation window. */
enum class ConfirmHit : uint8_t { None, Build, Cancel };

void ShowBuildConfirm(int x, int y);
void CloseBuildConfirm();
ConfirmHit GetBuildConfirmHit(int x, int y);

#endif /* MOBILE_BAR_GUI_H */
