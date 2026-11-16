/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file route_gui.h Touch "Route" tool: tap A, tap B, compare alternatives, build. */

#ifndef MOBILE_ROUTE_GUI_H
#define MOBILE_ROUTE_GUI_H

#include "../viewport_type.h"
#include "../rail_type.h"
#include "../gfx_type.h"

namespace Touch {

void StartRoutePlanning(RailType railtype);
bool IsRoutePlanning();
/** A map tap while planning: picks A or B. @return Whether the tap was used. */
bool HandleRouteTap(const Viewport &vp, int x, int y);
/** Draw the selected route on a viewport (window coordinates, \a dpi = area being drawn). */
void DrawRouteOverlay(const Viewport &vp, DrawPixelInfo *dpi);

} // namespace Touch

#endif /* MOBILE_ROUTE_GUI_H */
