/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file build_palette_gui.h Touch build palette (bottom sheet of large tool buttons). */

#ifndef MOBILE_BUILD_PALETTE_GUI_H
#define MOBILE_BUILD_PALETTE_GUI_H

#include "../core/geometry_type.hpp"

/** Build modes with a touch palette; order matches PALETTE_KINDS. */
enum class PaletteMode : uint8_t { Rail, Road, Tram, Water, Air, Landscape, End };

void ShowBuildPalette(PaletteMode mode);
void RepositionBuildPalette();
Point GetBuildPalettePosition(int width, int height);

#endif /* MOBILE_BUILD_PALETTE_GUI_H */
