#include <gtest/gtest.h>

#include "platform/lvgl_rotation.cpp"

#include <vector>

namespace {

// Naive reference: dst[col*V + row] = src[(V-1-row)*H + col]
// This mirrors the original inline loop in LvglPort::flushCb. Used as the
// oracle the tile-based implementation must match exactly.
void rotateNaive(const uint16_t* src, uint16_t* dst, uint16_t hor_res, uint16_t ver_res)
{
    for (uint16_t col = 0; col < hor_res; ++col) {
        for (uint16_t row = 0; row < ver_res; ++row) {
            dst[col * ver_res + row] = src[(ver_res - 1 - row) * hor_res + col];
        }
    }
}

void runAndCompare(uint16_t hor_res, uint16_t ver_res)
{
    std::vector<uint16_t> src(static_cast<size_t>(hor_res) * ver_res);
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint16_t>(i * 31u + 7u);
    }
    std::vector<uint16_t> dst_tiled(src.size(), 0);
    std::vector<uint16_t> dst_naive(src.size(), 0);

    rotateLandscape90(src.data(), dst_tiled.data(), hor_res, ver_res);
    rotateNaive(src.data(), dst_naive.data(), hor_res, ver_res);

    EXPECT_EQ(dst_tiled, dst_naive);
}

} // namespace

TEST(LvglRotation, HandCheckedSmallCase)
{
    // 4 × 3 source:
    //   1 2 3 4
    //   5 6 7 8
    //   9 A B C
    // Expected dst (col-major, rows reversed top-to-bottom):
    //   col 0: 9 5 1
    //   col 1: A 6 2
    //   col 2: B 7 3
    //   col 3: C 8 4
    constexpr uint16_t H = 4;
    constexpr uint16_t V = 3;
    const std::vector<uint16_t> src = {
        1,    2,    3,    4,
        5,    6,    7,    8,
        9,    0xA,  0xB,  0xC,
    };
    std::vector<uint16_t> dst(H * V, 0);
    rotateLandscape90(src.data(), dst.data(), H, V);

    const std::vector<uint16_t> expected = {
        9,    5,    1,
        0xA,  6,    2,
        0xB,  7,    3,
        0xC,  8,    4,
    };
    EXPECT_EQ(dst, expected);
}

TEST(LvglRotation, MatchesNaiveAtFullPanelSize)
{
    runAndCompare(640, 172);
}

TEST(LvglRotation, MatchesNaiveAtTileBoundaries)
{
    runAndCompare(16, 16);
    runAndCompare(32, 32);
    runAndCompare(64, 64);
}

TEST(LvglRotation, MatchesNaiveAtNonTileMultipleSizes)
{
    runAndCompare(17, 13);
    runAndCompare(173, 81);
    runAndCompare(640, 7);
    runAndCompare(7, 640);
}

TEST(LvglRotation, MatchesNaiveAtDegenerateSizes)
{
    runAndCompare(1, 1);
    runAndCompare(1, 172);
    runAndCompare(640, 1);
}

namespace {

// Produces a vector containing the slice [col_start, col_start+col_count) of
// the full rotation output, used to validate the chunked variant.
std::vector<uint16_t> sliceOfFull(const std::vector<uint16_t>& full,
                                  uint16_t ver_res,
                                  uint16_t col_start,
                                  uint16_t col_count)
{
    return std::vector<uint16_t>(
        full.begin() + static_cast<size_t>(col_start) * ver_res,
        full.begin() + static_cast<size_t>(col_start + col_count) * ver_res);
}

void runRangeAndCompare(uint16_t hor_res, uint16_t ver_res,
                        uint16_t col_start, uint16_t col_count)
{
    std::vector<uint16_t> src(static_cast<size_t>(hor_res) * ver_res);
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint16_t>(i * 17u + 5u);
    }
    std::vector<uint16_t> full(src.size(), 0);
    rotateLandscape90(src.data(), full.data(), hor_res, ver_res);

    std::vector<uint16_t> chunk(static_cast<size_t>(col_count) * ver_res, 0);
    rotateLandscape90Range(src.data(), chunk.data(), hor_res, ver_res,
                           col_start, col_count);

    EXPECT_EQ(chunk, sliceOfFull(full, ver_res, col_start, col_count))
        << "col_start=" << col_start << " col_count=" << col_count
        << " hor_res=" << hor_res << " ver_res=" << ver_res;
}

} // namespace

TEST(LvglRotation, RangeMatchesFullRotationSlice)
{
    runRangeAndCompare(640, 172, 0, 64);
    runRangeAndCompare(640, 172, 64, 64);
    runRangeAndCompare(640, 172, 576, 64);
    runRangeAndCompare(640, 172, 0, 640);
}

TEST(LvglRotation, RangeHandlesNonTileAlignedStart)
{
    runRangeAndCompare(640, 172, 7, 64);
    runRangeAndCompare(640, 172, 100, 173);
    runRangeAndCompare(640, 172, 23, 17);
}

TEST(LvglRotation, RangeClipsCountToHorRes)
{
    // col_start + col_count exceeds hor_res — should clip to hor_res - col_start.
    constexpr uint16_t H = 640;
    constexpr uint16_t V = 172;
    std::vector<uint16_t> src(static_cast<size_t>(H) * V);
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<uint16_t>(i);
    }
    std::vector<uint16_t> full(src.size(), 0);
    rotateLandscape90(src.data(), full.data(), H, V);

    std::vector<uint16_t> chunk(static_cast<size_t>(100) * V, 0);
    rotateLandscape90Range(src.data(), chunk.data(), H, V, 600, 100);

    // Only first 40 columns (600..639) should be written; rest stays zero.
    auto expected_slice = sliceOfFull(full, V, 600, 40);
    std::vector<uint16_t> expected(static_cast<size_t>(100) * V, 0);
    std::copy(expected_slice.begin(), expected_slice.end(), expected.begin());
    EXPECT_EQ(chunk, expected);
}
