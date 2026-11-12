# OpenTTD Mobile — Plan

Fork of [OpenTTD](https://github.com/OpenTTD/OpenTTD) (GPLv2) adding touch-first play on Android and Web/PWA.
Base: upstream `master` (SDL3 video driver, 16.0 cycle). Remote `upstream` tracks it.

## Goals / non-goals
- **Goals:** smooth play on a mid-range phone (≥ 60 fps at default zoom, 256×256 map), full touch control, multiplayer/savegame compatible with desktop OpenTTD, Android + Web from one codebase.
- **Non-goals (v1):** iOS (GPL vs App Store), new game mechanics, new art (ship OpenGFX/OpenSFX/OpenMSX).

## Principles
1. **Never touch the simulation.** All UI goes through the command layer (`Command<CMD_*>::Post`); desync-free by construction.
2. **Upstream-first for generic parts** (gesture input, DPI scale, safe-area insets). Fork-only for mobile GUI.
3. **Mobile GUI is additive:** new `src/mobile/` module behind `OPTION_MOBILE_GUI` (CMake) + runtime setting `gui.touch_mode`. Desktop GUI stays intact → cheap rebases.
4. Rebase onto upstream monthly; CI fails on conflicts early.

## Architecture
```
SDL3 events ─► src/video/sdl3_v.cpp ─► src/mobile/gesture.cpp ─► (a) synthesized mouse events → window.cpp (legacy windows)
  (finger, pen)                         tap/long-press/pan/pinch    (b) TouchGUI handlers → Command layer
```
- **Gesture layer** (`src/mobile/gesture.{h,cpp}`): consumes `SDL_EVENT_FINGER_*`; recognizes tap, double-tap, long-press (450 ms), pan (> 8 dp slop), two-finger pinch/pan. Disables SDL's touch→mouse emulation to avoid double events.
- **Viewport:** pinch → continuous zoom mapped to `ZoomLevel` steps with hysteresis; two-finger pan → `ScrollMainViewport`; inertial scroll (exp. decay, τ ≈ 325 ms).
- **Legacy window fallback:** any upstream window still usable — tap = left click, long-press = right click (tooltips/remove mode), drag = drag.
- **Scale:** auto `gui.scale` from display DPI (target 48 dp hit targets); safe-area insets (notch/gesture bar) reserved around toolbars.

## Touch UI spec (v1)
| Area | Desktop | Mobile |
|---|---|---|
| Main toolbar | 30 top icons | Bottom bar: Build · Vehicles · Finance · Menu; "Build" opens radial/grid sheet |
| Construction | Toolbar window + drag | Mode chip (rail/road/water/air/terraform) + drag-to-build with ghost preview and ✓/✕ confirm; undo last command |
| Signals | Separate window | Auto-signal spacing on drag; tap signal to cycle type |
| Context | Right-click | Long-press tile → radial menu (query, demolish, build station here, send vehicle) |
| Vehicle lists / orders / depots | Floating windows | Full-height bottom sheets, swipe between vehicles |
| News / messages | Popup windows | Toast + inbox |
| Text input | Keyboard | SDL text input → system IME |
| Pause / speed | Hotkeys | Persistent pill top-right; auto-pause on app background |

Priority order by usage: viewport gestures → construction → vehicle/orders/depot → station → finance/company → settings.

## Performance budget
- Frame: ≤ 16 ms on Snapdragon 7-series class; render via SDL3 OpenGL ES 3 / WebGL2 driver (`sdl3_opengl_v`), 32bpp blitter, animated 8bpp palette on GPU.
- Sim tick unchanged (30 ms); `fast forward` capped on mobile by thermal state.
- Web: WASM + pthreads (requires COOP/COEP headers), `-O3`, LTO; memory growth on; IDBFS for saves (existing `os/emscripten/pre.js`).
- Battery: drop to 30 fps when idle (no input, no animation dirty), pause on background.

## Milestones
| # | Deliverable | Exit criteria |
|---|---|---|
| M0 | Builds green: native Linux, Emscripten (existing `os/emscripten`), Android APK | Game starts, plays with mouse emulation on phone + browser |
| M1 | Gesture layer + viewport (pinch, pan, inertia), DPI auto-scale, safe areas | Navigate map comfortably; legacy windows usable by tap/long-press |
| M2 | Bottom bar + construction mode chips + drag-build with confirm | Build rail line + station + depot without opening legacy toolbar |
| M3 | Vehicle/orders/depot bottom sheets | Buy train, set orders, start it — touch only |
| M4 | PWA (manifest, service worker, offline content cache), Android polish (lifecycle, SAF save import/export, back gesture) | Installable PWA; Play/F-Droid-ready APK |
| M5 | Remaining windows, settings screen, onboarding tutorial scenario | Full game playable touch-only |

## Android build
- `os/android/`: Gradle project from SDL3 `android-project` template; CMake via `externalNativeBuild`; NDK r27+, `arm64-v8a` + `x86_64`; minSdk 26.
- Host tools (`strgen`, `settingsgen`, …) built natively first, passed via `HOST_BINARY_DIR` (same pattern as Emscripten).
- Deps via vcpkg android triplets (`vcpkg.json` already lists them).
- Base sets downloaded at build time and packaged as assets; extracted to app files dir on first run.
- Distribution: F-Droid (reproducible build) + Play; source link in About (GPL §3).

## CI (GitHub Actions)
- Reuse upstream `ci-build.yml` / `ci-emscripten.yml`; add `ci-android.yml` (APK artifact) and Pages deploy of Web build with COOP/COEP via service worker shim.
- Weekly job: rebase-test against `upstream/master`, report conflicts.

## Risks
- **Upstream window coupling** → mitigated by additive `src/mobile/` + fallback; still est. ~30 % merge overhead on GUI-heavy upstream changes.
- **WASM threads** need cross-origin isolation; GitHub Pages lacks headers → `coi-serviceworker` shim or alternate host.
- **Thermal throttling** on long sessions → fps cap + idle throttling.

## Open questions
- Portrait support in v1, or landscape-only? (Proposed: landscape v1, portrait M5.)
- App name/branding: must differ from "OpenTTD" trademark-wise unless upstream agrees.
