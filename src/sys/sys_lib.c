#include "sys.h"

#ifdef TARGET_PSP
void PspPlatform_LogLine(const char* line);
#define PSP_TRACE(msg) PspPlatform_LogLine("[psp] " msg)
#else
#define PSP_TRACE(msg) ((void) 0)
#endif

#ifdef TARGET_PSP
static void Psp_SetMtxElement(Mtx* m, s32 row, s32 col, f32 value) {
    s32 fixed = (s32) (value * 65536.0f);

    m->u.i[row][col] = (u16) ((fixed >> 16) & 0xFFFF);
    m->u.f[row][col] = (u16) (fixed & 0xFFFF);
}

static void Psp_MtxZero(Mtx* m) {
    s32 row;
    s32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            m->u.i[row][col] = 0;
            m->u.f[row][col] = 0;
        }
    }
}

static void Psp_MtxIdentity(Mtx* m) {
    Psp_MtxZero(m);
    Psp_SetMtxElement(m, 0, 0, 1.0f);
    Psp_SetMtxElement(m, 1, 1, 1.0f);
    Psp_SetMtxElement(m, 2, 2, 1.0f);
    Psp_SetMtxElement(m, 3, 3, 1.0f);
}

static void Psp_GuPerspective(Mtx* m, u16* perspNorm, f32 fovy, f32 aspect, f32 near, f32 far, f32 scale) {
    f32 cot;

    (void) fovy;

    PSP_TRACE("perspective: psp zero");
    Psp_MtxZero(m);

    /* Boot uses 45 degrees. Avoid libultra's double-heavy trig path on PSP hardware for now. */
    cot = 2.41421356237f;
    PSP_TRACE("perspective: psp fill");
    Psp_SetMtxElement(m, 0, 0, (cot / aspect) * scale);
    Psp_SetMtxElement(m, 1, 1, cot * scale);
    Psp_SetMtxElement(m, 2, 2, ((near + far) / (near - far)) * scale);
    Psp_SetMtxElement(m, 2, 3, -1.0f * scale);
    Psp_SetMtxElement(m, 3, 2, ((2.0f * near * far) / (near - far)) * scale);

    if (perspNorm != NULL) {
        if (near + far <= 2.0f) {
            *perspNorm = 0xFFFF;
        } else {
            *perspNorm = (u16) ((2.0f * 65536.0f) / (near + far));
            if (*perspNorm == 0) {
                *perspNorm = 1;
            }
        }
    }
    PSP_TRACE("perspective: psp filled");
}
#endif

s32 Lib_vsPrintf(char* dst, const char* fmt, va_list args) {
    return vsprintf(dst, fmt, args);
}

void Lib_vTable(s32 index, void (**table)(s32, s32), s32 arg0, s32 arg1) {
    void (*func)(s32, s32) = table[index];

    func(arg0, arg1);
}

void Lib_SwapBuffers(u8* buf1, u8* buf2, s32 len) {
    s32 i;
    u8 temp;

    for (i = 0; i < len; i++) {
        temp = buf2[i];
        buf2[i] = buf1[i];
        buf1[i] = temp;
    }
}

void Lib_QuickSort(u8* first, u32 length, u32 size, CompareFunc cFunc) {
    u32 splitIdx;
    u8* last;
    u8* right;
    u8* left;

    while (true) {
        last = first + (length - 1) * size;

        if (length == 2) {
            if (cFunc(first, last) > 0) {
                Lib_SwapBuffers(first, last, size);
            }
            return;
        }
        if (size && size && size) {} //! FAKE: must be here with at least 3 && operands.
        left = first;
        right = last - size;

        while (true) {
            while (cFunc(left, last) < 0) {
                left += size;
            }
            while ((cFunc(right, last) >= 0) && (left < right)) {
                right -= size;
            }
            if (left >= right) {
                break;
            }
            Lib_SwapBuffers(left, right, size);
            left += size;
            right -= size;
        }
        Lib_SwapBuffers(last, left, size);
        splitIdx = (left - first) / size;
        if (length / 2 < splitIdx) {
            if ((length - splitIdx) > 2) {
                Lib_QuickSort(left + size, length - splitIdx - 1, size, cFunc);
            }

            if (splitIdx < 2) {
                return;
            }
            left = first;
            length = splitIdx;
        } else {
            if (splitIdx >= 2) {
                Lib_QuickSort(first, splitIdx, size, cFunc);
            }

            if ((length - splitIdx) <= 2) {
                return;
            }

            first = left + size;
            length -= splitIdx + 1;
        }
    }
}

void Lib_InitPerspective(Gfx** dList) {
    u16 norm;

    PSP_TRACE("perspective: guPerspective");
#ifdef TARGET_PSP
    Psp_GuPerspective(gGfxMtx, &norm, gFovY, (f32) SCREEN_WIDTH / SCREEN_HEIGHT, gProjectNear, gProjectFar, 1.0f);
#else
    guPerspective(gGfxMtx, &norm, gFovY, (f32) SCREEN_WIDTH / SCREEN_HEIGHT, gProjectNear, gProjectFar, 1.0f);
#endif
    PSP_TRACE("perspective: persp normalize");
    gSPPerspNormalize((*dList)++, norm);
    PSP_TRACE("perspective: matrix 0");
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    PSP_TRACE("perspective: guLookAt");
#ifdef TARGET_PSP
    Psp_MtxIdentity(gGfxMtx);
#else
    guLookAt(gGfxMtx, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -12800.0f, 0.0f, 1.0f, 0.0f);
#endif
    PSP_TRACE("perspective: matrix 1");
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    PSP_TRACE("perspective: matrix copy");
    Matrix_Copy(gGfxMatrix, &gIdentityMatrix);
    PSP_TRACE("perspective: done");
}

void Lib_InitOrtho(Gfx** dList) {
    guOrtho(gGfxMtx, -SCREEN_WIDTH / 2, SCREEN_WIDTH / 2, -SCREEN_HEIGHT / 2, SCREEN_HEIGHT / 2, gProjectNear,
            gProjectFar, 1.0f);
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    guLookAt(gGfxMtx, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -12800.0f, 0.0f, 1.0f, 0.0f);
    gSPMatrix((*dList)++, gGfxMtx++, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    Matrix_Copy(gGfxMatrix, &gIdentityMatrix);
}

void Lib_DmaRead(void* src, void* dst, ptrdiff_t size) {
    osInvalICache(dst, size);
    osInvalDCache(dst, size);
    while (size > 0x100) {
        osPiStartDma(&gDmaIOMsg, 0, 0, (uintptr_t) src, dst, 0x100, &gDmaMesgQueue);
        size -= 0x100;
        src = (void*) ((uintptr_t) src + 0x100);
        dst = (void*) ((uintptr_t) dst + 0x100);
        MQ_WAIT_FOR_MESG(&gDmaMesgQueue, NULL);
    }
    if (size != 0) {
        osPiStartDma(&gDmaIOMsg, 0, 0, (uintptr_t) src, dst, size, &gDmaMesgQueue);
        MQ_WAIT_FOR_MESG(&gDmaMesgQueue, NULL);
    }
}

void Lib_FillScreen(u8 setFill) {
    s32 i;

    gFillScreenColor |= 1;
    if (setFill == true) {
        if (gFillScreen == false) {
            if (gFillScreenColor == 1) {
                osViBlack(true);
            } else {
                for (i = 0; i < 3 * SCREEN_WIDTH; i++) {
                    gFillBuffer[i] = gFillScreenColor;
                }
                osWritebackDCacheAll();
                osViSwapBuffer(&gFillBuffer[SCREEN_WIDTH]);
                osViRepeatLine(true);
            }
            gFillScreen = true;
        }
    } else if (gFillScreen == true) {
        osViRepeatLine(false);
        osViBlack(false);
        gFillScreen = false;
    }
}
