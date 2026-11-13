add_files(
    touch_input.cpp
    touch_input.h
    CONDITION ANDROID OR EMSCRIPTEN
)
