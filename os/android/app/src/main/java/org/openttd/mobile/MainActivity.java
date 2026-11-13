/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package org.openttd.mobile;

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
        try {
            extractDataIfNeeded();
        } catch (IOException e) {
            Log.e(TAG, "Extracting game data failed", e);
        }
        super.onCreate(savedInstanceState);
    }

    /** Native side chdirs to getFilesDir() (SDL_GetAndroidInternalStoragePath), so data lands there. */
    private void extractDataIfNeeded() throws IOException {
        File root = getFilesDir();
        File stamp = new File(root, STAMP);
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
