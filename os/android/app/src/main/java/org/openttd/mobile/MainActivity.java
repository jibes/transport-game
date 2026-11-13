/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package org.openttd.mobile;

import android.app.AlertDialog;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Extracts bundled game data to internal storage, then hands over to SDL (which calls SDL_main in libopenttd.so). */
public class MainActivity extends SDLActivity {
    private static final String TAG = "TransportMobile";
    private static final String ASSET_ROOT = "game";
    private static final String STAMP = ".data_version";
    private static final String LAUNCH_STAMP = ".last_launch";
    /** A previous run whose log stopped this soon after launch counts as a failed start. */
    private static final long FAILED_START_MS = 20_000;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "openttd" };
    }

    /** Verbose driver/startup logging while the port is young; shows up in logcat tag "openttd". */
    @Override
    protected String[] getArguments() {
        return new String[] { "-d", "driver=5,misc=3,grf=1" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        boolean previousFailed = checkPreviousRun();
        try {
            extractDataIfNeeded();
        } catch (IOException e) {
            Log.e(TAG, "Extracting game data failed", e);
        }
        super.onCreate(savedInstanceState);
        if (previousFailed) offerLogShare();
    }

    /**
     * Detects a start that died quickly (log last written < FAILED_START_MS after launch) and keeps its
     * log as failed_run.log, since the native side overwrites last_run.log on every start.
     */
    private boolean checkPreviousRun() {
        File launch = new File(getFilesDir(), LAUNCH_STAMP);
        File log = new File(getFilesDir(), LogShare.LAST_RUN);
        boolean failed = false;
        if (launch.exists() && log.exists()) {
            long ranFor = log.lastModified() - launch.lastModified();
            if (ranFor >= 0 && ranFor < FAILED_START_MS) {
                try {
                    Files.copy(log.toPath(), new File(getFilesDir(), LogShare.FAILED_RUN).toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                    failed = true;
                } catch (IOException e) {
                    Log.e(TAG, "Saving failed-run log failed", e);
                }
            }
        }
        try {
            new FileOutputStream(launch).close(); // touch: mtime = this launch
            launch.setLastModified(System.currentTimeMillis());
        } catch (IOException e) {
            Log.e(TAG, "Writing launch stamp failed", e);
        }
        return failed;
    }

    private void offerLogShare() {
        new AlertDialog.Builder(this)
            .setTitle("Last start failed")
            .setMessage("The game closed shortly after starting last time. Share the log so it can be fixed?")
            .setPositiveButton("Share log", (d, w) -> LogShare.share(this, new File(getFilesDir(), LogShare.FAILED_RUN)))
            .setNegativeButton("Not now", null)
            .show();
    }

    /**
     * Native side sets HOME=getFilesDir(); OpenTTD then always searches its XDG personal dir
     * ($HOME/.local/share/openttd/) for baseset/, lang/, ai/, game/. The working dir is not searched
     * because it equals the parent of the personal dir.
     */
    private void extractDataIfNeeded() throws IOException {
        File root = new File(getFilesDir(), ".local/share/openttd");
        File stamp = new File(getFilesDir(), STAMP);
        String version = currentVersion();
        if (stamp.exists() && version.equals(new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8))) return;

        copyTree(getAssets(), ASSET_ROOT, root);
        try (OutputStream out = new FileOutputStream(stamp)) {
            out.write(version.getBytes(StandardCharsets.UTF_8));
        }
    }

    private String currentVersion() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            return pi.versionName + "/" + pi.lastUpdateTime;
        } catch (Exception e) {
            return "unknown";
        }
    }

    private static void copyTree(AssetManager am, String assetPath, File dest) throws IOException {
        String[] children = am.list(assetPath);
        if (children == null || children.length == 0) {
            copyFile(am, assetPath, dest);
            return;
        }
        for (String child : children) {
            copyTree(am, assetPath + "/" + child, new File(dest, child));
        }
    }

    private static void copyFile(AssetManager am, String assetPath, File dest) throws IOException {
        File parent = dest.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) throw new IOException("mkdirs " + parent);
        try (InputStream in = am.open(assetPath); OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }
}
