# Mobile UI tests

Touch-driven checks of the Web build in a phone-sized headless Chromium.

1. Build the Web version (`os/emscripten/README.md`), then serve it: `cd build-web && python3 -m http.server 8765`.
2. Run a scenario, e.g. a fresh game with a fixed map, portrait:

   ```
   ARGS="-g -G 1" VW=412 VH=915 node tests/mobile/touch.js /tmp/out tests/mobile/smoke.json
   ```

`ARGS` is passed to the game via the page URL (`?args=`, see `os/emscripten/pre.js`). `-g` starts a new game without
the menu and `-G <seed>` fixes the map, so coordinates in scenarios stay valid between runs.
