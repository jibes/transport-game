/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_sheet_gui.h Compact touch controls for the rail station picker. */

#ifndef MOBILE_STATION_SHEET_GUI_H
#define MOBILE_STATION_SHEET_GUI_H

struct Window;

namespace Touch {

/** Keep the compact station sheet in step with the rail toolbar's station picker (call regularly). */
void SyncStationSheet(Window *rail_toolbar);

} // namespace Touch

#endif /* MOBILE_STATION_SHEET_GUI_H */
