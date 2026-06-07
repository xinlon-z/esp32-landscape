#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Transpose a row-major hor_res × ver_res 16-bpp source buffer into a
// column-major ver_res × hor_res destination buffer with source rows
// reversed top-to-bottom — i.e.,
//
//     dst[col * ver_res + row] = src[(ver_res - 1 - row) * hor_res + col]
//
// This is the operation the AXS15231B's portrait scan needs from LVGL's
// landscape draw buffer when full_refresh = 1 and the panel is wired in
// portrait. Both buffers must be 16-bit pixel buffers of size at least
// hor_res * ver_res.
void rotateLandscape90(const uint16_t* src, uint16_t* dst,
                       uint16_t hor_res, uint16_t ver_res);

// Same operation as rotateLandscape90 but only emits the slice corresponding
// to source columns [col_start, col_start + col_count). dst is laid out as
// col_count × ver_res (column-major). Used by the streaming flush path so
// each chunk's rotation can overlap with the previous chunk's DMA.
//
// If col_start + col_count exceeds hor_res, col_count is clipped down. If
// col_start >= hor_res, the call is a no-op.
void rotateLandscape90Range(const uint16_t* src, uint16_t* dst,
                            uint16_t hor_res, uint16_t ver_res,
                            uint16_t col_start, uint16_t col_count);

#ifdef __cplusplus
}
#endif
