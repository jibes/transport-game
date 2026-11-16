/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file card_gui.h Tap-to-select cards (touch). */

#ifndef MOBILE_CARD_GUI_H
#define MOBILE_CARD_GUI_H

#include "../viewport_type.h"
#include "../vehicle_type.h"
#include "../station_type.h"

namespace Touch {

/**
 * A tap on the map without an active tool: show a card for the vehicle or tile there.
 * @return Whether a card was shown; otherwise the game handles the click as usual.
 */
bool ShowCardForClick(const Viewport &vp, int x, int y, const Vehicle *v);
void ShowVehicleCard(VehicleID vehicle);
void ShowStationCard(StationID station);

} // namespace Touch

#endif /* MOBILE_CARD_GUI_H */
