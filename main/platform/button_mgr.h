#pragma once

struct ButtonManagerCallbacks {
    void (*toggle_screen)() = nullptr;
    void (*go_home)() = nullptr;
};

class ButtonManager {
public:
    static void init(const ButtonManagerCallbacks& callbacks);

    // Test hook: pressed=true means the physical low-active button is down.
    static void processForTest(bool boot_pressed, bool pwr_pressed);
    static void resetForTest(const ButtonManagerCallbacks& callbacks = {});

private:
    static void task(void*);
};
