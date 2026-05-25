#include "../main/app/features/clock/clock_model.cpp"

#include <stdio.h>
#include <string.h>

int main()
{
    ClockModel model;
    ClockDisplayState invalid = model.buildTime(false, 0, 0, 0, 0, 0, 0);
    if (strcmp(invalid.time, "--:--") != 0 || strcmp(invalid.weekday, "RTC") != 0 || strcmp(invalid.date, "--/--") != 0) {
        printf("invalid RTC display failed\n");
        return 1;
    }

    ClockDisplayState valid = model.buildTime(true, 9, 5, 2, 1, 5, 24);
    if (strcmp(valid.time, "09:05") != 0 || strcmp(valid.weekday, "Mon") != 0 || strcmp(valid.date, "05/24") != 0) {
        printf("valid RTC display failed: %s %s %s\n", valid.time, valid.weekday, valid.date);
        return 1;
    }

    ClockDisplayState blink = model.buildTime(true, 9, 5, 3, 1, 5, 24);
    if (strcmp(blink.time, "09 05") != 0) {
        printf("colon blink failed: %s\n", blink.time);
        return 1;
    }
    return 0;
}
