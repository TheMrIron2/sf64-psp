/*
 * LEGACY RENDERER NOTICE
 *
 * This include file belongs to the retired direct-GU renderer and is not part
 * of the current PSP build. The active renderer is the PSPGL path, and the
 * authoritative PSP source list is src/psp/sources.mk. This file is retained
 * only as legacy/reference code.
 */

static void psp_renderer_draw_starfield_batch(void) {
    PspVertex2DColor* vertices;
    u32 i;
    u32 out = 0;

    if (!sStarfieldReady || (sStarfieldCount == 0)) {
        return;
    }

    vertices = (PspVertex2DColor*) sceGuGetMemory(sStarfieldCount * 6 * sizeof(PspVertex2DColor));
    if (vertices == NULL) {
        sRenderer.census.validationFailures++;
        sStarfieldReady = 0;
        return;
    }

    for (i = 0; i < sStarfieldCount; i++) {
        PspStarPoint* star = &sStarfieldStars[i];
        s16 x0 = (s16) PSP_COORD_X(star->x);
        s16 y0 = (s16) PSP_COORD_Y(star->y);
        s16 x1 = (s16) PSP_COORD_X(star->x + 1);
        s16 y1 = (s16) PSP_COORD_Y(star->y + 1);
        u32 color = star->color;

        vertices[out].color = color;
        vertices[out].x = x0;
        vertices[out].y = y0;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x1;
        vertices[out].y = y0;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x0;
        vertices[out].y = y1;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x1;
        vertices[out].y = y0;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x1;
        vertices[out].y = y1;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x0;
        vertices[out].y = y1;
        vertices[out].z = 0;
        out++;
    }

    sceKernelDcacheWritebackRange(vertices, out * sizeof(PspVertex2DColor));

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(
        GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        out,
        0,
        vertices
    );

    /* Consume once so a frame that does not submit stars does not draw stale data. */
    sStarfieldReady = 0;
}

void PspRenderer_BeginStarfield(void) {
    sStarfieldCount = 0;
    sStarfieldReady = 0;
}

void PspRenderer_AddStar(s16 x, s16 y, u32 color) {
    PspStarPoint* star;

    if (sStarfieldCount >= ARRAY_COUNT(sStarfieldStars)) {
        return;
    }

    star = &sStarfieldStars[sStarfieldCount++];
    star->x = x;
    star->y = y;
    star->color = color;
}

void PspRenderer_EndStarfield(void) {
    sStarfieldReady = 1;
}

void PspRenderer_DrawPendingStarfield(void) {
    psp_renderer_draw_starfield_batch();
}
