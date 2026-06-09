#pragma once

class LvglPort {
public:
    static bool lock(int = -1) { return true; }
    static void unlock() {}
};
