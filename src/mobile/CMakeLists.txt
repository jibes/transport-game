add_files(
    build_palette_gui.cpp
    build_palette_gui.h
    mobile_bar_gui.cpp
    mobile_bar_gui.h
    touch_input.cpp
    touch_input.h
    CONDITION ANDROID OR EMSCRIPTEN
)
