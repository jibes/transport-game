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

## Decisions (2026-11)
- **Audience:** newcomers first. Assists on by default; a "Classic" switch turns them off.
- **Upstream:** rebaseable. New UI lives in `src/mobile/`; upstream files get only small, `WITH_MOBILE_GUI`-guarded hooks.
- **Orientation:** portrait and landscape are equal. Every flow is tested in both.
- **Multiplayer:** not needed now, but kept possible. Every action is a standard command and nothing is client-only that changes game state, so there is no snapshot undo; preview, confirm and "remove last build" instead.
- **Name:** "Transport Mobile" is a placeholder. Strings are English first.

## UX v2: fun-first design

**Model:** Perceive → **Decide** → Act → Feedback. Fun lives in *Decide* and in learning from *Feedback*. *Perceive* and *Act* are costs to minimise.

**Automation line:** automate a step only if a skilled player would not choose differently there. Where there is a real trade-off, show the options and their consequences, and let the player choose.

**Rules:**
1. Decisions stay with the player; the game never computes "the answer".
2. Information appears where the decision is made: tap a thing to get its card; overlays replace windows.
3. Input states intent (A→B, add a stop, double a line); the game carries out the mechanics.
4. Everything is previewed with its cost before it happens, and can be reverted.
5. Feedback comes back where the action happened: profit per line, "why" notes.
6. Effort follows frequency: frequent actions take 1 tap, rare ones 2–3 via More….
7. Sessions fit a phone: autosave when the app goes to the background, easy pause.

| Decision | What the player sees | How the player acts | What the player learns |
|---|---|---|---|
| Where to serve | supply/demand overlay; town and industry cards | — | — |
| How to connect | route alternatives with cost, length and slopes | tap A and B, pick an alternative, drag via points, ✓ | "why it costs more" note |
| What vehicles | comparison view next to the cargo waiting | buy, `– n +` wagons | load-factor bar |
| When to expand | cash trend, alerts | one-tap actions on alerts | income per line |
| What to fix | problems inbox | jump there, quick fixes | badge clears |

## Implementation plan v2

| Phase | Item | Hook into upstream | Effort |
|---|---|---|---|
| **0. Tests** | Deterministic map seed via a URL parameter (`?seed=`) on the Web build; touch test scripts in `tests/mobile/` running portrait and landscape | `os/emscripten/pre.js` / shell passes arguments | S |
| **A. Foundation** | A1: autosave when the app goes to the background, and a "Continue" button on the main menu | SDL background events | S |
| | A2: tap-to-select **cards** (vehicle, station, depot, town, industry) with information, 48 dp actions, and "More…" for the original window | one-line hooks before the game opens its vehicle, station, depot, town and industry windows | M |
| | A3: assists settings with a Guided/Classic master switch | — | S |
| **B. Building** | B1: **auto-route** planner (A* over rail track pieces, bridges, existing own track at no cost), 3 weightings (cheap, balanced, straight), route preview, total cost from the game's cost check, build via the normal track and bridge commands | a viewport overlay hook to draw the route | M–L |
| | B2: station placement with coverage shown live, and size and rotation in the card instead of the small pickers | — | M |
| **C. Vehicles** | C1: orders by tapping stations on the map (route mode) | — | M |
| | C2: vehicle comparison view, wagon stepper, depot card | — | M |
| **D. Feedback** | D1: supply/demand and cargo-flow overlays (reuse the game's cargo-flow overlay) | — | M |
| | D2: profit per line, problems inbox, "why" notes | — | M |
| **E. Onboarding** | E1: first-run guided scenario (connect two towns), built from the same cards and flows | — | M |

Order: 0 → A1 → A2 → B1 → C1/C2 → D → B2 → E. Each item goes on a feature branch, passes CI (Android emulator check and Web), then merges into `main`, which publishes it to Obtainium and Pages.
