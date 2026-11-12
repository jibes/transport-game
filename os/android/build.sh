#!/usr/bin/env bash
# Build Android APK: host tools -> deps (SDL3, liblzma) -> libopenttd.so -> stage data -> gradle.
# Usage: os/android/build.sh [abi]   (env: ANDROID_NDK_HOME, ANDROID_HOME; default abi arm64-v8a)
set -euo pipefail

ABI="${1:-arm64-v8a}"
API=26
SDL_TAG=release-3.4.2
XZ_TAG=v5.4.6
OPENGFX=7.1
OPENSFX=1.0.3

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="$ROOT/build-android-deps"
PREFIX="$DEPS/prefix-$ABI"
NDK="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}"
TOOLCHAIN=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
           -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_static
           -DCMAKE_BUILD_TYPE=Release)
JOBS="$(nproc)"

mkdir -p "$DEPS"

# 1. Host tools (strgen, settingsgen, ...), run during the cross build.
if [ ! -f "$ROOT/build-host/src/strgen/strgen" ] && [ ! -f "$ROOT/build-host/strgen" ]; then
	cmake -S "$ROOT" -B "$ROOT/build-host" -DOPTION_TOOLS_ONLY=ON
	cmake --build "$ROOT/build-host" -j "$JOBS" --target tools
fi

# 2. Dependencies.
[ -d "$DEPS/SDL" ] || git clone --depth 1 -b "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$DEPS/SDL"
if [ ! -f "$PREFIX/lib/libSDL3.so" ]; then
	cmake -S "$DEPS/SDL" -B "$DEPS/build-sdl-$ABI" "${TOOLCHAIN[@]}" \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
		-DCMAKE_INSTALL_PREFIX="$PREFIX"
	cmake --build "$DEPS/build-sdl-$ABI" -j "$JOBS" --target install
fi

[ -d "$DEPS/xz" ] || git clone --depth 1 -b "$XZ_TAG" https://github.com/tukaani-project/xz.git "$DEPS/xz"
if [ ! -f "$PREFIX/lib/liblzma.a" ]; then
	cmake -S "$DEPS/xz" -B "$DEPS/build-xz-$ABI" "${TOOLCHAIN[@]}" \
		-DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_TESTING=OFF \
		-DCMAKE_INSTALL_PREFIX="$PREFIX"
	cmake --build "$DEPS/build-xz-$ABI" -j "$JOBS" --target liblzma
	cmake --install "$DEPS/build-xz-$ABI" || true
fi

# 3. Game library.
BUILD="$ROOT/build-android-$ABI"
cmake -S "$ROOT" -B "$BUILD" "${TOOLCHAIN[@]}" \
	-DHOST_BINARY_DIR="$ROOT/build-host" \
	-DCMAKE_FIND_ROOT_PATH="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
	-DOPTION_USE_ASSERTS=OFF
cmake --build "$BUILD" -j "$JOBS" --target openttd

# 4. Stage native libs + data for gradle.
APP="$ROOT/os/android/app/src/main"
rm -rf "${APP:?}/jniLibs/${ABI:?}" "${APP:?}/assets/game"
mkdir -p "$APP/jniLibs/$ABI" "$APP/assets/game/baseset" "$APP/assets/game/lang"
cp "$PREFIX/lib/libSDL3.so" "$BUILD/libopenttd.so" "$APP/jniLibs/$ABI/"
cp -r "$BUILD/baseset/." "$APP/assets/game/baseset/"
cp "$BUILD"/lang/*.lng "$APP/assets/game/lang/"
cp -r "$ROOT/bin/ai" "$ROOT/bin/game" "$APP/assets/game/"

fetch_set() { # name version
	local zip="$DEPS/$1-$2-all.zip"
	[ -f "$zip" ] || curl -fsSL -o "$zip" "https://cdn.openttd.org/$1-releases/$2/$1-$2-all.zip"
	unzip -o -q "$zip" -d "$APP/assets/game/baseset/"
}
fetch_set opengfx "$OPENGFX"
fetch_set opensfx "$OPENSFX"

# 5. APK.
cd "$ROOT/os/android"
gradle --no-daemon -PsdlSrc="$DEPS/SDL" assembleDebug
echo "APK: $(ls "$ROOT"/os/android/app/build/outputs/apk/debug/*.apk)"
