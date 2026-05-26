#if PSP_RENDERER_DIAGNOSTICS
static const char* psp_renderer_light_variant_name(void) {
#if PSP_RENDERER_LIGHT_VARIANT == PSP_LIGHT_VARIANT_RAW
    return "raw";
#elif PSP_RENDERER_LIGHT_VARIANT == PSP_LIGHT_VARIANT_MODELVIEW_ROW
    return "modelview-row";
#elif PSP_RENDERER_LIGHT_VARIANT == PSP_LIGHT_VARIANT_MODELVIEW_COLUMN_NEGATED
    return "modelview-column-negated";
#else
    return "modelview-column";
#endif
}

static char* psp_renderer_append_text(char* out, const char* text) {
    while ((text != NULL) && (*text != '\0')) {
        *out++ = *text++;
    }
    return out;
}

static char* psp_renderer_append_u32(char* out, u32 value) {
    char digits[10];
    s32 count = 0;

    if (value == 0) {
        *out++ = '0';
        return out;
    }

    while (value != 0) {
        digits[count++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        *out++ = digits[--count];
    }
    return out;
}

static void psp_renderer_log_pair(char** out, const char* label, u32 value) {
    *out = psp_renderer_append_text(*out, label);
    *out = psp_renderer_append_u32(*out, value);
}

static char* psp_renderer_append_s32(char* out, s32 value) {
    if (value < 0) {
        *out++ = '-';
        value = -value;
    }
    return psp_renderer_append_u32(out, (u32) value);
}

static void psp_renderer_log_signed_pair(char** out, const char* label, s32 value) {
    *out = psp_renderer_append_text(*out, label);
    *out = psp_renderer_append_s32(*out, value);
}

static void psp_renderer_log_light_transform(s32 rawX, s32 rawY, s32 rawZ, f32 transformedX, f32 transformedY,
                                             f32 transformedZ, f32 normalizedX, f32 normalizedY, f32 normalizedZ) {
    char line[256];

    if (sRenderer.census.lightTransformLogged != 0) {
        return;
    }
    sRenderer.census.lightTransformLogged = 1;
    snprintf(line, sizeof(line),
             "[psp] renderer light transform variant %s raw %ld,%ld,%ld transformed %ld,%ld,%ld normalized %ld,%ld,%ld mode %lu",
             psp_renderer_light_variant_name(),
             (long) rawX,
             (long) rawY,
             (long) rawZ,
             (long) (transformedX * 1000.0f),
             (long) (transformedY * 1000.0f),
             (long) (transformedZ * 1000.0f),
             (long) (normalizedX * 1000.0f),
             (long) (normalizedY * 1000.0f),
             (long) (normalizedZ * 1000.0f),
             (unsigned long) sRenderer.rsp.mode);
    PspPlatform_LogLine(line);
}

static void psp_renderer_record_shade_sample(u32 color, f32 dotSum, u32 dotCount) {
    u32 r = color & 0xFF;
    u32 g = (color >> 8) & 0xFF;
    u32 b = (color >> 16) & 0xFF;

    if (sRenderer.census.shadeSampleCount == 0) {
        sRenderer.census.shadeMinR = r;
        sRenderer.census.shadeMinG = g;
        sRenderer.census.shadeMinB = b;
        sRenderer.census.shadeMaxR = r;
        sRenderer.census.shadeMaxG = g;
        sRenderer.census.shadeMaxB = b;
    } else {
        if (r < sRenderer.census.shadeMinR) {
            sRenderer.census.shadeMinR = r;
        }
        if (g < sRenderer.census.shadeMinG) {
            sRenderer.census.shadeMinG = g;
        }
        if (b < sRenderer.census.shadeMinB) {
            sRenderer.census.shadeMinB = b;
        }
        if (r > sRenderer.census.shadeMaxR) {
            sRenderer.census.shadeMaxR = r;
        }
        if (g > sRenderer.census.shadeMaxG) {
            sRenderer.census.shadeMaxG = g;
        }
        if (b > sRenderer.census.shadeMaxB) {
            sRenderer.census.shadeMaxB = b;
        }
    }

    sRenderer.census.shadeSampleCount++;
    sRenderer.census.shadeSumR += r;
    sRenderer.census.shadeSumG += g;
    sRenderer.census.shadeSumB += b;
    if (dotCount != 0) {
        sRenderer.census.shadeLitCount++;
        sRenderer.census.shadeDotMilliSum += (s32) (dotSum * 1000.0f);
        sRenderer.census.shadeDotSampleCount += dotCount;
    }
}

static u8 psp_renderer_top_unsupported(void) {
    u32 i;
    u32 bestCount = 0;
    u8 bestOpcode = 0;

    for (i = 0; i < ARRAY_COUNT(sRenderer.census.unsupported); i++) {
        if (sRenderer.census.unsupported[i] > bestCount) {
            bestCount = sRenderer.census.unsupported[i];
            bestOpcode = (u8) i;
        }
    }
    return bestOpcode;
}

static void psp_renderer_log_census(u32 taskIndex) {
    char line[256];
    char* out = line;

    if ((taskIndex > 4) && ((taskIndex % 30) != 0)) {
        return;
    }

    out = psp_renderer_append_text(out, "[psp] renderer:");
    psp_renderer_log_pair(&out, " task ", taskIndex);
    psp_renderer_log_pair(&out, " bytes ", sRenderer.census.taskBytes);
    psp_renderer_log_pair(&out, " cmds ", sRenderer.census.commandCount);
    psp_renderer_log_pair(&out, " dl ", sRenderer.census.dlCount);
    psp_renderer_log_pair(&out, " fill ", sRenderer.census.fillRectCount);
    psp_renderer_log_pair(&out, " tex ", sRenderer.census.texRectCount + sRenderer.census.texRectFlipCount);
    psp_renderer_log_pair(&out, " drawn ", sRenderer.census.texturedRectCount + sRenderer.census.placeholderRectCount);
    psp_renderer_log_pair(&out, " skip ", sRenderer.census.texRectDrawSkipped);
    psp_renderer_log_pair(&out, " bad ", sRenderer.census.validationFailures + sRenderer.census.rangeRejects);
    *out = '\0';
    PspPlatform_LogLine(line);

    if (sRenderer.census.unsupportedCount != 0) {
        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer unsupported:");
        psp_renderer_log_pair(&out, " kinds ", sRenderer.census.unsupportedCount);
        psp_renderer_log_pair(&out, " top ", psp_renderer_top_unsupported());
        *out = '\0';
        PspPlatform_LogLine(line);
    }

    if (sRenderer.rsp.lightCount != 0) {
        const PspRspLight* light = &sRenderer.rsp.lights[0];

        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer light:");
        psp_renderer_log_pair(&out, " count ", sRenderer.rsp.lightCount);
        psp_renderer_log_pair(&out, " ambR ", sRenderer.rsp.ambientR);
        psp_renderer_log_pair(&out, " ambG ", sRenderer.rsp.ambientG);
        psp_renderer_log_pair(&out, " ambB ", sRenderer.rsp.ambientB);
        psp_renderer_log_pair(&out, " l0R ", light->r);
        psp_renderer_log_pair(&out, " l0G ", light->g);
        psp_renderer_log_pair(&out, " l0B ", light->b);
        psp_renderer_log_signed_pair(&out, " x ", light->x);
        psp_renderer_log_signed_pair(&out, " y ", light->y);
        psp_renderer_log_signed_pair(&out, " z ", light->z);
        *out = '\0';
        PspPlatform_LogLine(line);
    }

    if (sRenderer.census.shadeSampleCount != 0) {
        u32 avgR = sRenderer.census.shadeSumR / sRenderer.census.shadeSampleCount;
        u32 avgG = sRenderer.census.shadeSumG / sRenderer.census.shadeSampleCount;
        u32 avgB = sRenderer.census.shadeSumB / sRenderer.census.shadeSampleCount;
        s32 avgDot = 0;

        if (sRenderer.census.shadeDotSampleCount != 0) {
            avgDot = sRenderer.census.shadeDotMilliSum / (s32) sRenderer.census.shadeDotSampleCount;
        }

        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer shade:");
        psp_renderer_log_pair(&out, " samples ", sRenderer.census.shadeSampleCount);
        psp_renderer_log_pair(&out, " lit ", sRenderer.census.shadeLitCount);
        psp_renderer_log_pair(&out, " minR ", sRenderer.census.shadeMinR);
        psp_renderer_log_pair(&out, " minG ", sRenderer.census.shadeMinG);
        psp_renderer_log_pair(&out, " minB ", sRenderer.census.shadeMinB);
        psp_renderer_log_pair(&out, " avgR ", avgR);
        psp_renderer_log_pair(&out, " avgG ", avgG);
        psp_renderer_log_pair(&out, " avgB ", avgB);
        psp_renderer_log_pair(&out, " maxR ", sRenderer.census.shadeMaxR);
        psp_renderer_log_pair(&out, " maxG ", sRenderer.census.shadeMaxG);
        psp_renderer_log_pair(&out, " maxB ", sRenderer.census.shadeMaxB);
        psp_renderer_log_signed_pair(&out, " avgDotM ", avgDot);
        psp_renderer_log_pair(&out, " combine ", (u32) sRenderer.rdp.combineMode);
        psp_renderer_log_pair(&out, " mod ", sRenderer.census.texFuncModulateCount);
        psp_renderer_log_pair(&out, " repl ", sRenderer.census.texFuncReplaceCount);
        *out = '\0';
        PspPlatform_LogLine(line);
    }
}
#else
static void psp_renderer_log_census(u32 taskIndex) {
    (void) taskIndex;
}
#endif
