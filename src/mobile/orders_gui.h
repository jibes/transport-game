/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file orders_gui.h Touch stop picking: tap stations on the map to add them to a vehicle's orders. */

#ifndef MOBILE_ORDERS_GUI_H
#define MOBILE_ORDERS_GUI_H

#include "../viewport_type.h"
#include "../vehicle_type.h"

namespace Touch {

void StartStopPicking(VehicleID vehicle);
/** A map tap while picking stops. @return Whether the tap was used. */
bool HandleStopTap(const Viewport &vp, int x, int y);

} // namespace Touch

#endif /* MOBILE_ORDERS_GUI_H */
