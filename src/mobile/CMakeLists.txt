add_files(
    mobile_bar_gui.cpp
    mobile_bar_gui.h
    touch_input.cpp
    touch_input.h
    CONDITION ANDROID OR EMSCRIPTEN
)
