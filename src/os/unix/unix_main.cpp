/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file unix_main.cpp Main entry for Unix. */

#include "../../stdafx.h"
#include "../../openttd.h"
#include "../../crashlog.h"
#include "../../core/random_func.hpp"
#include "../../string_func.h"

#include <time.h>
#include <signal.h>

#ifdef __ANDROID__
/* SDL_main.h renames main() to SDL_main(), which SDLActivity calls from libopenttd.so. */
#	include <SDL3/SDL_main.h>
#	include <SDL3/SDL_system.h>
#	include <stdlib.h>
#	include <unistd.h>
#	include <stdio.h>
#	include <android/log.h>
#	include <thread>

/** Forward stdout/stderr to logcat (tag "openttd") and to last_run.log, as Android discards both. */
static void RedirectOutputToLogcat()
{
	static int pfd[2];
	if (pipe(pfd) != 0) return;
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	dup2(pfd[1], STDOUT_FILENO);
	dup2(pfd[1], STDERR_FILENO);
	std::thread([] {
		FILE *in = fdopen(pfd[0], "r");
		FILE *log = fopen("last_run.log", "w");
		char line[1024];
		while (in != nullptr && fgets(line, sizeof(line), in) != nullptr) {
			__android_log_write(ANDROID_LOG_INFO, "openttd", line);
			if (log != nullptr) { fputs(line, log); fflush(log); }
		}
	}).detach();
}
#endif

#include "../../safeguards.h"

int CDECL main(int argc, char *argv[])
{
#ifdef __ANDROID__
	/* Game data is extracted by the Java side to the internal storage path; use it as working and home dir. */
	if (const char *dir = SDL_GetAndroidInternalStoragePath(); dir != nullptr) {
		setenv("HOME", dir, 1);
		if (chdir(dir) != 0) return 1;
	}
	RedirectOutputToLogcat();
#endif

	/* Make sure our arguments contain only valid UTF-8 characters. */
	std::vector<std::string_view> params;
	for (int i = 0; i < argc; ++i) {
		StrMakeValidInPlace(argv[i]);
		params.emplace_back(argv[i]);
	}

	CrashLog::InitialiseCrashLog();

	SetRandomSeed(time(nullptr));

	signal(SIGPIPE, SIG_IGN);

	return openttd_main(params);
}
