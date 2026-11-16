/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file lifecycle.cpp Phone app lifecycle: never lose progress.
 *
 * Phones stop or kill apps in the background without warning. So:
 *  - when the app goes to the background, the running game is saved to "resume.sav";
 *  - on the next start of a touch device, that save is loaded instead of showing the menu;
 *  - choosing "exit to main menu" deliberately removes it, so the menu shows next time.
 */

#include "../stdafx.h"
#include "lifecycle.h"
#include "touch_input.h"
#include "../openttd.h"
#include "../fileio_func.h"
#include "../fios.h"
#include "../network/network.h"
#include "../saveload/saveload_func.h"
#include "../saveload/saveload_type.h"
#include "../debug.h"

#include <filesystem>

#include "../safeguards.h"

namespace Touch {

static constexpr std::string_view RESUME_SAVE = "resume.sav";

void SaveForResume()
{
	if (_game_mode != GameMode::Normal || _networking) return;
	/* Synchronous (threaded = false): the OS may stop us right after this returns. */
	if (SaveOrLoad(RESUME_SAVE, SaveLoadOperation::Save, DetailedFileType::GameFile, Subdirectory::Save, false) != SaveLoadResult::Ok) {
		Debug(Facility::Misc, Severity::Error, "Saving {} for resume failed", RESUME_SAVE);
	}
}

void TryResume()
{
	if (!IsTouchDevice() || _switch_mode != SwitchMode::Menu) return;
	if (!FioCheckFileExists(RESUME_SAVE, Subdirectory::Save)) return;
	_file_to_saveload.name = FioFindFullPath(Subdirectory::Save, RESUME_SAVE);
	_file_to_saveload.SetMode(FIOS_TYPE_FILE, SaveLoadOperation::Load);
	_switch_mode = SwitchMode::LoadGame;
}

void ForgetResume()
{
	if (!FioCheckFileExists(RESUME_SAVE, Subdirectory::Save)) return;
	std::error_code ec;
	std::filesystem::remove(OTTD2FS(FioFindFullPath(Subdirectory::Save, RESUME_SAVE)), ec);
}

} // namespace Touch
