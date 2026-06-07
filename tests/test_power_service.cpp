#include <gtest/gtest.h>
#include "app/services/power_service.cpp"
#include "app/core/event/event_bus.cpp"

TEST(PowerService, SnapshotFields)
{
    PowerSnapshot snapshot{};
    snapshot.external_power = true;
    snapshot.battery_percent = 87;
    snapshot.dimmed = true;
    snapshot.sleeping = false;
    snapshot.revision = 5;

    EXPECT_TRUE(snapshot.external_power) << "snapshot fields failed";
    EXPECT_EQ(snapshot.battery_percent, 87) << "snapshot fields failed";
    EXPECT_TRUE(snapshot.dimmed) << "snapshot fields failed";
    EXPECT_FALSE(snapshot.sleeping) << "snapshot fields failed";
    EXPECT_EQ(snapshot.revision, 5) << "revision failed";
}
