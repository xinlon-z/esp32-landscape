#include <gtest/gtest.h>
#include "app/features/music/widgets/background_image.cpp"
#include "app/features/music/util/music_background.cpp"

namespace {
uint8_t redOf(lv_color_t color)
{
    return static_cast<uint8_t>((lv_color_to32(color) >> 16) & 0xff);
}

uint8_t blueOf(lv_color_t color)
{
    return static_cast<uint8_t>(lv_color_to32(color) & 0xff);
}
} // namespace

TEST(MusicBackgroundImage, BuildAndRelease)
{
    constexpr uint16_t cover_w = 144;
    constexpr uint16_t cover_h = 144;
    lv_color_t cover_pixels[cover_w * cover_h];
    for (uint16_t y = 0; y < cover_h; ++y) {
        for (uint16_t x = 0; x < cover_w; ++x) {
            cover_pixels[y * cover_w + x] = x < cover_w / 2
                                                ? lv_color_make(240, 16, 24)
                                                : lv_color_make(20, 36, 232);
        }
    }

    lv_img_dsc_t cover_image{};
    cover_image.header.w = cover_w;
    cover_image.header.h = cover_h;
    cover_image.header.cf = LV_IMG_CF_TRUE_COLOR;
    cover_image.data = reinterpret_cast<const uint8_t*>(cover_pixels);
    cover_image.data_size = sizeof(cover_pixels);

    BorrowedCover cover{};
    cover.cover_id = 7;
    cover.status = CoverStatus::Ready;
    cover.image = &cover_image;
    cover.pixels = cover_pixels;

    MusicBackgroundImage background{};
    EXPECT_TRUE(musicBuildBackgroundImage(cover, 640, 172, &background))
        << "ready cover should build a blurred background image";
    EXPECT_EQ(background.image.header.w, 640) << "background width should match display";
    EXPECT_EQ(background.image.header.h, 172) << "background height should match display";
    EXPECT_TRUE(background.image.data == reinterpret_cast<const uint8_t*>(background.pixels))
        << "background descriptor should point at owned pixels";
    EXPECT_TRUE(background.image.data_size == 640u * 172u * sizeof(lv_color_t))
        << "background descriptor should expose full framebuffer size";

    const lv_color_t left = background.pixels[86 * 640 + 8];
    const lv_color_t right = background.pixels[86 * 640 + 632];
    EXPECT_GT(redOf(left), blueOf(left)) << "left background should retain cover color influence";
    EXPECT_GT(blueOf(right), redOf(right)) << "right background should retain cover color influence";

    musicReleaseBackgroundImage(&background);
    EXPECT_TRUE(background.pixels == nullptr) << "release should clear owned pixels";
    EXPECT_TRUE(background.image.data == nullptr) << "release should clear image descriptor";
}
