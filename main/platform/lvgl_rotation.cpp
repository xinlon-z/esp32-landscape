#include "lvgl_rotation.h"

#include <string.h>

namespace {

constexpr uint16_t kTileR = 16;
constexpr uint16_t kTileC = 16;

} // namespace

void rotateLandscape90Range(const uint16_t* src, uint16_t* dst,
                            uint16_t hor_res, uint16_t ver_res,
                            uint16_t col_start, uint16_t col_count)
{
    if (!src || !dst || hor_res == 0 || ver_res == 0 || col_count == 0 ||
        col_start >= hor_res) {
        return;
    }
    if (col_start + col_count > hor_res) {
        col_count = static_cast<uint16_t>(hor_res - col_start);
    }
    const uint16_t col_end = static_cast<uint16_t>(col_start + col_count);

    uint16_t local[kTileR * kTileC];

    for (uint16_t rt = 0; rt < ver_res; rt += kTileR) {
        const uint16_t rows = (rt + kTileR > ver_res) ? static_cast<uint16_t>(ver_res - rt)
                                                       : kTileR;
        // The output rows [rt .. rt + rows - 1] correspond to source rows
        // [ver_res - 1 - rt .. ver_res - rt - rows] (descending). Reading in
        // ascending source order keeps reads contiguous inside the tile and
        // contiguous between tiles.
        const uint16_t source_row_start = static_cast<uint16_t>(ver_res - rt - rows);

        for (uint16_t ct = col_start; ct < col_end; ct += kTileC) {
            const uint16_t cols = (ct + kTileC > col_end) ? static_cast<uint16_t>(col_end - ct)
                                                          : kTileC;

            // Stage 1: read tile into a stack-local staging buffer with
            // contiguous source rows. This pulls one cache line per row of
            // the tile — under the 32-byte SPIRAM cache line at 16 px × 2 B.
            for (uint16_t r = 0; r < rows; ++r) {
                memcpy(&local[r * kTileC],
                       &src[(source_row_start + r) * hor_res + ct],
                       cols * sizeof(uint16_t));
            }

            // Stage 2: write tile to dst with the inner loop running along
            // the dst column direction (rt+r contiguous), so each output
            // column is written linearly — also one cache line per column.
            for (uint16_t c = 0; c < cols; ++c) {
                uint16_t* out_col = &dst[(ct - col_start + c) * ver_res + rt];
                for (uint16_t r = 0; r < rows; ++r) {
                    out_col[r] = local[(rows - 1 - r) * kTileC + c];
                }
            }
        }
    }
}

void rotateLandscape90(const uint16_t* src, uint16_t* dst,
                       uint16_t hor_res, uint16_t ver_res)
{
    rotateLandscape90Range(src, dst, hor_res, ver_res, 0, hor_res);
}
