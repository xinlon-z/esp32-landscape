#include <gtest/gtest.h>

#include <string.h>
#include <vector>

// Test-only access shim: opens up the service's private members for this
// translation unit so tests can inspect internal state. Production code is
// unaffected — this is a textual hack scoped to this file.
#define private public
#define protected public
#include "app/features/music/widgets/background_blur_service.cpp"
#include "app/features/music/widgets/background_image.cpp"
#include "app/features/music/util/music_background.cpp"
#undef private
#undef protected

namespace {
struct RegisteredCover {
    uint32_t cover_id = 0;
    const lv_color_t* pixels = nullptr;
    uint16_t w = 0;
    uint16_t h = 0;
};

std::vector<RegisteredCover>& registeredCovers()
{
    static std::vector<RegisteredCover> covers;
    return covers;
}

constexpr uint16_t kCoverW = 32;
constexpr uint16_t kCoverH = 32;
constexpr uint16_t kBgW = 64;
constexpr uint16_t kBgH = 16;

void fillCover(std::vector<lv_color_t>& pixels, lv_color_t color)
{
    pixels.assign(kCoverW * kCoverH, color);
}

BorrowedCover makeBorrowedCover(uint32_t cover_id,
                                std::vector<lv_color_t>& pixels,
                                lv_img_dsc_t& img,
                                lv_color_t color = lv_color_make(0x80, 0x40, 0xc0))
{
    fillCover(pixels, color);
    img = lv_img_dsc_t{};
    img.header.w = kCoverW;
    img.header.h = kCoverH;
    img.header.cf = LV_IMG_CF_TRUE_COLOR;
    img.data = reinterpret_cast<const uint8_t*>(pixels.data());
    img.data_size = pixels.size() * sizeof(lv_color_t);

    BorrowedCover cover{};
    cover.cover_id = cover_id;
    cover.status = CoverStatus::Ready;
    cover.image = &img;
    cover.pixels = pixels.data();

    registeredCovers().push_back(RegisteredCover{
        cover.cover_id,
        pixels.data(),
        static_cast<uint16_t>(img.header.w),
        static_cast<uint16_t>(img.header.h),
    });
    return cover;
}

void seedCache(BackgroundBlurService& svc, uint32_t cover_id)
{
    const size_t count = static_cast<size_t>(kBgW) * kBgH;
    svc.cache_.pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(count * sizeof(lv_color_t), 0));
    svc.cache_.image.header.w = kBgW;
    svc.cache_.image.header.h = kBgH;
    svc.cache_.image.header.cf = LV_IMG_CF_TRUE_COLOR;
    svc.cache_.image.data = reinterpret_cast<const uint8_t*>(svc.cache_.pixels);
    svc.cache_.image.data_size = count * sizeof(lv_color_t);
    svc.cache_cover_id_ = cover_id;
}

void seedStale(BackgroundBlurService& svc)
{
    const size_t count = static_cast<size_t>(kBgW) * kBgH;
    svc.stale_.pixels = static_cast<lv_color_t*>(
        heap_caps_malloc(count * sizeof(lv_color_t), 0));
    svc.stale_.image.header.w = kBgW;
    svc.stale_.image.header.h = kBgH;
    svc.stale_.image.header.cf = LV_IMG_CF_TRUE_COLOR;
    svc.stale_.image.data = reinterpret_cast<const uint8_t*>(svc.stale_.pixels);
    svc.stale_.image.data_size = count * sizeof(lv_color_t);
}
} // namespace

// BackgroundBlurService only needs CoverService's locked pixel-copy surface here;
// keep this test focused by using a tiny local double instead of the JPEG service.
CoverService::CoverService() = default;

CoverService& CoverService::get()
{
    static CoverService service;
    return service;
}

bool CoverService::copyPixels(uint32_t cover_id,
                              lv_color_t* dst_pixels,
                              uint32_t dst_pixel_count,
                              lv_img_dsc_t* out_image)
{
    for (const RegisteredCover& cover : registeredCovers()) {
        if (cover.cover_id != cover_id || !cover.pixels || cover.w == 0 || cover.h == 0) {
            continue;
        }

        const uint32_t pixel_count = static_cast<uint32_t>(cover.w) * cover.h;
        if (pixel_count > dst_pixel_count) {
            return false;
        }

        memcpy(dst_pixels, cover.pixels, pixel_count * sizeof(lv_color_t));
        if (out_image) {
            *out_image = lv_img_dsc_t{};
            out_image->header.w = cover.w;
            out_image->header.h = cover.h;
            out_image->header.cf = LV_IMG_CF_TRUE_COLOR;
            out_image->data = reinterpret_cast<const uint8_t*>(dst_pixels);
            out_image->data_size = pixel_count * sizeof(lv_color_t);
        }
        return true;
    }
    return false;
}

class BackgroundBlurServiceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        lvglStubState().reset();
        lvglPortStubState().reset();
        registeredCovers().clear();
    }
};

TEST_F(BackgroundBlurServiceTest, InitialStateIsEmpty)
{
    BackgroundBlurService svc;
    const lv_img_dsc_t* img = nullptr;
    EXPECT_FALSE(svc.tryGetCached(0, &img));
    EXPECT_FALSE(svc.tryGetCached(99, &img));
    EXPECT_FALSE(svc.has_pending_);
    EXPECT_EQ(svc.cache_.pixels, nullptr);
}

TEST_F(BackgroundBlurServiceTest, RequestRejectsInvalidInput)
{
    BackgroundBlurService svc;

    BorrowedCover empty{};
    EXPECT_FALSE(svc.request(empty, kBgW, kBgH, nullptr));

    std::vector<lv_color_t> pixels;
    lv_img_dsc_t img{};
    BorrowedCover cover = makeBorrowedCover(7, pixels, img);
    EXPECT_FALSE(svc.request(cover, 0, kBgH, nullptr));
    EXPECT_FALSE(svc.request(cover, kBgW, 0, nullptr));
    EXPECT_FALSE(svc.has_pending_);
}

TEST_F(BackgroundBlurServiceTest, RequestQueuesPendingOnCacheMiss)
{
    BackgroundBlurService svc;
    std::vector<lv_color_t> pixels;
    lv_img_dsc_t img{};
    BorrowedCover cover = makeBorrowedCover(7, pixels, img);

    const lv_img_dsc_t* out = nullptr;
    EXPECT_FALSE(svc.request(cover, kBgW, kBgH, &out));
    EXPECT_EQ(out, nullptr);
    EXPECT_TRUE(svc.has_pending_);
    EXPECT_EQ(svc.pending_.cover_id, 7u);
    ASSERT_NE(svc.pending_.src_pixels, nullptr);
    EXPECT_NE(svc.pending_.src_pixels, pixels.data())
        << "src pixels must be an internal copy, not the caller's buffer";
}

TEST_F(BackgroundBlurServiceTest, DuplicateRequestForSamePendingCoverIsIdempotent)
{
    BackgroundBlurService svc;
    std::vector<lv_color_t> pixels;
    lv_img_dsc_t img{};
    BorrowedCover cover = makeBorrowedCover(7, pixels, img);

    EXPECT_FALSE(svc.request(cover, kBgW, kBgH, nullptr));
    const lv_color_t* first_src = svc.pending_.src_pixels;

    EXPECT_FALSE(svc.request(cover, kBgW, kBgH, nullptr));
    EXPECT_EQ(svc.pending_.src_pixels, first_src)
        << "second request for same cover should not reallocate the pending copy";
}

TEST_F(BackgroundBlurServiceTest, RequestForDifferentCoverReplacesPending)
{
    BackgroundBlurService svc;
    std::vector<lv_color_t> p1, p2;
    lv_img_dsc_t img1{}, img2{};
    const lv_color_t c1_color = lv_color_make(0xff, 0x00, 0x00);
    const lv_color_t c2_color = lv_color_make(0x00, 0x00, 0xff);
    BorrowedCover c1 = makeBorrowedCover(11, p1, img1, c1_color);
    BorrowedCover c2 = makeBorrowedCover(22, p2, img2, c2_color);

    EXPECT_FALSE(svc.request(c1, kBgW, kBgH, nullptr));
    ASSERT_NE(svc.pending_.src_pixels, nullptr);

    EXPECT_FALSE(svc.request(c2, kBgW, kBgH, nullptr));
    EXPECT_EQ(svc.pending_.cover_id, 22u);
    ASSERT_NE(svc.pending_.src_pixels, nullptr);
    EXPECT_EQ(svc.pending_.src_pixels[0], c2_color)
        << "pending slot should now hold a copy of cover 2's pixels";
}

TEST_F(BackgroundBlurServiceTest, RequestReturnsTrueOnCacheHit)
{
    BackgroundBlurService svc;
    seedCache(svc, 42);

    std::vector<lv_color_t> pixels;
    lv_img_dsc_t img{};
    BorrowedCover cover = makeBorrowedCover(42, pixels, img);

    const lv_img_dsc_t* out = nullptr;
    EXPECT_TRUE(svc.request(cover, kBgW, kBgH, &out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->header.w, kBgW);
    EXPECT_FALSE(svc.has_pending_) << "cache hit must not enqueue a job";
}

TEST_F(BackgroundBlurServiceTest, TryGetCachedReturnsCachedImage)
{
    BackgroundBlurService svc;
    seedCache(svc, 99);

    const lv_img_dsc_t* out = nullptr;
    EXPECT_TRUE(svc.tryGetCached(99, &out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->header.w, kBgW);

    EXPECT_FALSE(svc.tryGetCached(100, &out)) << "different cover_id misses";
    EXPECT_FALSE(svc.tryGetCached(0, &out)) << "cover_id 0 always misses";
}

TEST_F(BackgroundBlurServiceTest, ClearReadyCallbackOnlyMatchesUserData)
{
    BackgroundBlurService svc;
    int marker = 0;
    auto cb = [](uint32_t, void* ud) { (*static_cast<int*>(ud))++; };

    svc.setReadyCallback(cb, &marker);
    EXPECT_EQ(svc.ready_cb_, cb);

    int other = 0;
    svc.clearReadyCallback(&other);
    EXPECT_EQ(svc.ready_cb_, cb)
        << "clearing with a non-matching user_data must not unbind";

    svc.clearReadyCallback(&marker);
    EXPECT_EQ(svc.ready_cb_, nullptr);
    EXPECT_EQ(svc.ready_user_data_, nullptr);
}

TEST_F(BackgroundBlurServiceTest, OnAsyncReadyInvokesUserCallback)
{
    BackgroundBlurService svc;
    seedCache(svc, 7);

    struct Captured {
        uint32_t cover_id = 0;
        int calls = 0;
    } captured;

    svc.setReadyCallback(
        [](uint32_t cover_id, void* ud) {
            auto* c = static_cast<Captured*>(ud);
            c->cover_id = cover_id;
            c->calls++;
        },
        &captured);

    BackgroundBlurService::onAsyncReady(&svc);

    EXPECT_EQ(captured.calls, 1);
    EXPECT_EQ(captured.cover_id, 7u);
}

TEST_F(BackgroundBlurServiceTest, OnAsyncReadyDefersStaleFreeViaTimer)
{
    BackgroundBlurService svc;
    seedCache(svc, 1);
    seedStale(svc);
    ASSERT_NE(svc.stale_.pixels, nullptr);

    BackgroundBlurService::onAsyncReady(&svc);
    EXPECT_EQ(lvglStubState().timer_creates, 1)
        << "stale free should be scheduled, not run inline";
    EXPECT_NE(svc.stale_.pixels, nullptr)
        << "stale must remain alive until the timer fires";

    ASSERT_NE(lvglStubState().last_timer, nullptr);
    BackgroundBlurService::onStaleTimer(lvglStubState().last_timer);
    EXPECT_EQ(svc.stale_.pixels, nullptr)
        << "timer firing must release the stale pixels";
    EXPECT_GE(lvglStubState().img_cache_invalidations, 1);
}

TEST_F(BackgroundBlurServiceTest, OnAsyncReadyFallsBackWhenTimerCreateFails)
{
    BackgroundBlurService svc;
    seedCache(svc, 1);
    seedStale(svc);
    ASSERT_NE(svc.stale_.pixels, nullptr);

    lvglStubState().fail_timer_create = true;
    BackgroundBlurService::onAsyncReady(&svc);

    EXPECT_EQ(svc.stale_.pixels, nullptr)
        << "when timer creation fails, stale should be freed inline";
}

TEST_F(BackgroundBlurServiceTest, OnStaleTimerIsSafeWhenStaleAlreadyEmpty)
{
    BackgroundBlurService svc;
    lv_timer_t timer{};
    timer.user_data = &svc;

    BackgroundBlurService::onStaleTimer(&timer);
    EXPECT_EQ(svc.stale_.pixels, nullptr);
}
