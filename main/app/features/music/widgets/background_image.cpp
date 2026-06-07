#include "background_image.h"

#include "esp_heap_caps.h"

void musicReleaseBackgroundImage(MusicBackgroundImage* background)
{
    if (!background) {
        return;
    }
    if (background->pixels) {
        heap_caps_free(background->pixels);
    }
    *background = MusicBackgroundImage{};
}
