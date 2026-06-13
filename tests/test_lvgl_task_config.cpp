#include <gtest/gtest.h>

#include "platform/lvgl_task_config.h"

TEST(LvglTaskConfig, StackBudgetCoversTinyTtfRendering)
{
    EXPECT_GE(lvglTaskStackBytes(), 12u * 1024u);
    EXPECT_GT(lvglStackWarnBytes(), 0u);
    EXPECT_LT(lvglStackWarnBytes(), lvglTaskStackBytes());
}
