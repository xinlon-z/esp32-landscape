#pragma once

// Host stub for LvglPort. Tracks lock/unlock counts so tests can assert that
// the worker correctly takes the LVGL lock before calling lv_async_call.

struct LvglPortStubState {
    int lock_calls = 0;
    int unlock_calls = 0;
    bool fail_lock = false;

    void reset()
    {
        lock_calls = 0;
        unlock_calls = 0;
        fail_lock = false;
    }
};

inline LvglPortStubState& lvglPortStubState()
{
    static LvglPortStubState state;
    return state;
}

class LvglPort {
public:
    static bool lock(int = -1)
    {
        auto& s = lvglPortStubState();
        s.lock_calls++;
        return !s.fail_lock;
    }
    static void unlock()
    {
        lvglPortStubState().unlock_calls++;
    }
};
