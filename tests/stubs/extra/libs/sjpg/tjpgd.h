#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    JDR_OK = 0,
    JDR_ERROR = 1,
} JRESULT;

typedef struct {
    uint16_t width;
    uint16_t height;
    void* device;
} JDEC;

typedef struct {
    uint16_t left;
    uint16_t right;
    uint16_t top;
    uint16_t bottom;
} JRECT;

typedef size_t (*JpegInputFn)(JDEC*, uint8_t*, size_t);
typedef int (*JpegOutputFn)(JDEC*, void*, JRECT*);

static inline JRESULT jd_prepare(JDEC* jd, JpegInputFn, void*, size_t, void* device)
{
    if (!jd || !device) {
        return JDR_ERROR;
    }
    jd->width = 2;
    jd->height = 2;
    jd->device = device;
    return JDR_OK;
}

static inline JRESULT jd_decomp(JDEC* jd, JpegOutputFn output, uint8_t)
{
    if (!jd || !output) {
        return JDR_ERROR;
    }
    uint8_t rgb[] = {
        0xff, 0x00, 0x00,
        0x00, 0xff, 0x00,
        0x00, 0x00, 0xff,
        0xff, 0xff, 0xff,
    };
    JRECT rect{0, 1, 0, 1};
    return output(jd, rgb, &rect) ? JDR_OK : JDR_ERROR;
}
