#pragma once

namespace app {

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual void onTick() = 0;
};

} // namespace app
