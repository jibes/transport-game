/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package org.openttd.mobile;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;

/** Log files written by the native side (unix_main.cpp) and helpers to share them as plain text. */
final class LogShare {
    static final String LAST_RUN = "last_run.log";
    static final String FAILED_RUN = "failed_run.log";
    private static final int MAX_BYTES = 200 * 1024; // stay well below the Binder transaction limit

    private LogShare() {}

    /** Preferred log: the saved failed run if any, else the most recent run. */
    static File bestLog(Context ctx) {
        File failed = new File(ctx.getFilesDir(), FAILED_RUN);
        return failed.exists() ? failed : new File(ctx.getFilesDir(), LAST_RUN);
    }

    static void share(Activity activity, File log) {
        StringBuilder text = new StringBuilder()
            .append("App: ").append(BuildConfigInfo.version(activity)).append('\n')
            .append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
            .append(", Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n")
            .append("ABI: ").append(Build.SUPPORTED_ABIS[0]).append('\n')
            .append("Log: ").append(log.getName()).append("\n\n")
            .append(tail(log));

        Intent send = new Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_SUBJECT, "Transport Mobile log")
            .putExtra(Intent.EXTRA_TEXT, text.toString());
        activity.startActivity(Intent.createChooser(send, "Share log"));
    }

    /** Last MAX_BYTES of the file; logs grow from startup, but crashes are at the end. */
    private static String tail(File f) {
        if (!f.exists()) return "(no log file yet — start the game once)";
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            long len = raf.length();
            long start = Math.max(0, len - MAX_BYTES);
            byte[] buf = new byte[(int) (len - start)];
            raf.seek(start);
            raf.readFully(buf);
            return (start > 0 ? "[... truncated ...]\n" : "") + new String(buf, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "(could not read log: " + e + ")";
        }
    }

    /** Version string without depending on a generated BuildConfig. */
    static final class BuildConfigInfo {
        static String version(Context ctx) {
            try {
                return ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionName;
            } catch (Exception e) {
                return "unknown";
            }
        }
    }
}
