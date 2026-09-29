#define PSP_HW_TEXTURE_HISTORY_COUNT 512
#define PSP_HW_TEXTURE_SOURCE_COUNT 256
#define PSP_HW_TEXTURE_FORMAT_COUNT 6
#define PSP_HW_TEXTURE_CHANGE_COUNT 8

typedef enum {
    PSP_HW_TEXTURE_UNKNOWN,
    PSP_HW_TEXTURE_INVALIDATED_KEY,
    PSP_HW_TEXTURE_EVICTED_KEY,
    PSP_HW_TEXTURE_CHANGED_VARIANT,
    PSP_HW_TEXTURE_REPEAT_KEY,
    PSP_HW_TEXTURE_CAUSE_COUNT
} PspHwTextureCause;

typedef enum {
    PSP_HW_TEXTURE_ATTEMPTS,
    PSP_HW_TEXTURE_FAILURES,
    PSP_HW_TEXTURE_UPLOADS,
    PSP_HW_TEXTURE_SOURCE_PIXELS,
    PSP_HW_TEXTURE_STORED_TEXELS,
    PSP_HW_TEXTURE_STORAGE_BYTES,
    PSP_HW_TEXTURE_EDRAM_UPLOADS,
    PSP_HW_TEXTURE_REFRESHES,
    PSP_HW_TEXTURE_CREATE_US,
    PSP_HW_TEXTURE_PREPARE_US,
    PSP_HW_TEXTURE_RESERVE_US,
    PSP_HW_TEXTURE_DECODE_US,
    PSP_HW_TEXTURE_WRITEBACK_US,
    PSP_HW_TEXTURE_METRIC_COUNT
} PspHwTextureMetric;

typedef struct {
    PspGfxTextureRequest key;
    u32 serial;
    PspHwTextureCause state;
} PspHwTextureHistory;

typedef struct {
    const void* pixels;
    PspGfxTextureFormat format;
    u64 metric[PSP_HW_TEXTURE_METRIC_COUNT];
    u32 cause[PSP_HW_TEXTURE_CAUSE_COUNT];
    u32 changes[PSP_HW_TEXTURE_CHANGE_COUNT];
} PspHwTextureSource;

static PspHwTextureHistory sPspHwTextureHistory[PSP_HW_TEXTURE_HISTORY_COUNT];
static PspHwTextureSource sPspHwTextureSources[PSP_HW_TEXTURE_SOURCE_COUNT + 1];
static u64 sPspHwTextureCreates[PSP_HW_TEXTURE_FORMAT_COUNT][PSP_HW_TEXTURE_CAUSE_COUNT][PSP_HW_TEXTURE_METRIC_COUNT];
static u64 sPspHwTextureRetires[PSP_HW_TEXTURE_FORMAT_COUNT][PSP_HW_TEXTURE_RETIRE_COUNT][4];
static u32 sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_COUNT];
static u32 sPspHwTextureSerial;
static u32 sPspHwTextureHistoryNext;
static u32 sPspHwTextureHistoryReplacements;
static u32 sPspHwTextureSourceCount;

static const char* sPspHwTextureFormatNames[] = { "ci8", "ci4", "rgba16", "rgba32", "ia8", "ia16" };
static const char* sPspHwTextureCauseNames[] = {
    "unseen_or_history_gap", "invalidated_key", "evicted_key", "changed_variant", "repeat_without_retirement"
};
static const char* sPspHwTextureRetireNames[] = {
    "source_invalidation", "slot_pressure", "edram_byte_budget", "edram_allocation", "ram_byte_budget"
};
static const char* sPspHwTextureMetricNames[] = {
    "attempts", "failures", "uploads", "source_pixels", "stored_texels", "storage_bytes", "edram_uploads",
    "mutable_refreshes", "create_us", "prepare_us", "reserve_us", "decode_us", "writeback_us"
};
static const char* sPspHwTextureFrameNames[] = {
    "create_us", "reserve_us", "decode_us", "writeback_us", "storage_bytes", "uploads", "failures",
    "invalidated_uploads", "evicted_uploads", "variant_uploads"
};
static const char* sPspHwTextureChangeNames[] = {
    "palette", "dimensions", "premultiply", "soft_coverage", "env_blend", "mirror", "primitive_color", "environment_color"
};

static void psp_hw_texture_reset(void) {
    memset(sPspHwTextureHistory, 0, sizeof(sPspHwTextureHistory));
    memset(sPspHwTextureSources, 0, sizeof(sPspHwTextureSources));
    memset(sPspHwTextureCreates, 0, sizeof(sPspHwTextureCreates));
    memset(sPspHwTextureRetires, 0, sizeof(sPspHwTextureRetires));
    memset(sPspHwTextureFrame, 0, sizeof(sPspHwTextureFrame));
    sPspHwTextureSerial = 0;
    sPspHwTextureHistoryNext = 0;
    sPspHwTextureHistoryReplacements = 0;
    sPspHwTextureSourceCount = 0;
}

static u32 psp_hw_texture_changes(const PspGfxTextureRequest* a, const PspGfxTextureRequest* b) {
    return ((a->palette != b->palette) << 0) |
           (((a->width != b->width) || (a->height != b->height)) << 1) |
           ((a->premultiply != b->premultiply) << 2) |
           ((a->softCoverage != b->softCoverage) << 3) |
           ((a->envBlend != b->envBlend) << 4) |
           (((a->mirrorS != b->mirrorS) || (a->mirrorT != b->mirrorT)) << 5) |
           ((a->primitiveColor != b->primitiveColor) << 6) |
           ((a->environmentColor != b->environmentColor) << 7);
}

static u32 psp_hw_texture_history(const PspGfxTextureRequest* request, PspHwTextureCause* cause, u32* changes) {
    u32 i;
    u32 latest = PSP_HW_TEXTURE_HISTORY_COUNT;
    u32 serial = 0;
    PspHwTextureHistory* history;

    *cause = PSP_HW_TEXTURE_UNKNOWN;
    *changes = 0;
    for (i = 0; i < PSP_HW_TEXTURE_HISTORY_COUNT; i++) {
        history = &sPspHwTextureHistory[i];
        if ((history->serial == 0) || (history->key.pixels != request->pixels) ||
            (history->key.format != request->format)) {
            continue;
        }
        if (psp_hw_texture_changes(&history->key, request) == 0) {
            *cause = history->state;
            history->serial = ++sPspHwTextureSerial;
            return i;
        }
        if (history->serial > serial) {
            serial = history->serial;
            latest = i;
        }
    }
    if (latest != PSP_HW_TEXTURE_HISTORY_COUNT) {
        *cause = PSP_HW_TEXTURE_CHANGED_VARIANT;
        *changes = psp_hw_texture_changes(&sPspHwTextureHistory[latest].key, request);
    }
    i = sPspHwTextureHistoryNext++ % PSP_HW_TEXTURE_HISTORY_COUNT;
    history = &sPspHwTextureHistory[i];
    if (history->serial != 0) {
        sPspHwTextureHistoryReplacements++;
    }
    history->key = *request;
    history->serial = ++sPspHwTextureSerial;
    history->state = PSP_HW_TEXTURE_UNKNOWN;
    return i;
}

static u32 psp_hw_texture_source(const PspGfxTextureRequest* request) {
    u32 i;

    for (i = 0; i < sPspHwTextureSourceCount; i++) {
        if ((sPspHwTextureSources[i].pixels == request->pixels) &&
            (sPspHwTextureSources[i].format == request->format)) {
            return i;
        }
    }
    if (i < PSP_HW_TEXTURE_SOURCE_COUNT) {
        sPspHwTextureSources[i].pixels = request->pixels;
        sPspHwTextureSources[i].format = request->format;
        sPspHwTextureSourceCount++;
    }
    return i;
}

void PspHwCounterProfile_TextureCreateBegin(const PspGfxTextureRequest* request, PspHwTextureCreateSample* sample) {
    PspHwTextureCause cause;

    memset(sample, 0, sizeof(*sample));
    if (!sPspHwFrameArmed || ((u32) request->format >= PSP_HW_TEXTURE_FORMAT_COUNT)) {
        return;
    }
    sample->active = 1;
    sample->history = psp_hw_texture_history(request, &cause, &sample->changes);
    sample->cause = cause;
    sample->source = psp_hw_texture_source(request);
    sample->startUs = sample->lastUs = sceKernelGetSystemTimeLow();
}

void PspHwCounterProfile_TextureCreateStage(PspHwTextureCreateSample* sample, PspHwTextureStage stage) {
    u32 now;

    if (!sample->active) {
        return;
    }
    now = sceKernelGetSystemTimeLow();
    sample->stageUs[stage] = now - sample->lastUs;
    sample->lastUs = now;
}

void PspHwCounterProfile_TextureCreateEnd(const PspGfxTextureRequest* request, PspHwTextureCreateSample* sample,
                                        u32 bytes, u32 texels, int edram, int refresh) {
    u32 values[PSP_HW_TEXTURE_METRIC_COUNT];
    u64* totals;
    PspHwTextureSource* source;
    u32 i;

    if (!sample->active) {
        return;
    }
    values[PSP_HW_TEXTURE_CREATE_US] = sceKernelGetSystemTimeLow() - sample->startUs;
    values[PSP_HW_TEXTURE_ATTEMPTS] = 1;
    values[PSP_HW_TEXTURE_FAILURES] = (bytes == 0);
    values[PSP_HW_TEXTURE_UPLOADS] = (bytes != 0);
    values[PSP_HW_TEXTURE_SOURCE_PIXELS] = bytes ? request->width * request->height : 0;
    values[PSP_HW_TEXTURE_STORED_TEXELS] = texels;
    values[PSP_HW_TEXTURE_STORAGE_BYTES] = bytes;
    values[PSP_HW_TEXTURE_EDRAM_UPLOADS] = bytes && edram;
    values[PSP_HW_TEXTURE_REFRESHES] = bytes && refresh;
    for (i = 0; i < PSP_HW_TEXTURE_STAGE_COUNT; i++) {
        values[PSP_HW_TEXTURE_PREPARE_US + i] = sample->stageUs[i];
    }
    totals = sPspHwTextureCreates[request->format][sample->cause];
    source = &sPspHwTextureSources[sample->source];
    for (i = 0; i < PSP_HW_TEXTURE_METRIC_COUNT; i++) {
        totals[i] += values[i];
        source->metric[i] += values[i];
    }
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_CREATE_US] += values[PSP_HW_TEXTURE_CREATE_US];
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_RESERVE_US] += values[PSP_HW_TEXTURE_RESERVE_US];
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_DECODE_US] += values[PSP_HW_TEXTURE_DECODE_US];
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_WRITEBACK_US] += values[PSP_HW_TEXTURE_WRITEBACK_US];
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_BYTES] += bytes;
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_UPLOADS] += (bytes != 0);
    sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_FAILURES] += (bytes == 0);
    if (bytes != 0) {
        source->cause[sample->cause]++;
        for (i = 0; i < PSP_HW_TEXTURE_CHANGE_COUNT; i++) {
            source->changes[i] += (sample->changes >> i) & 1U;
        }
        if ((sample->cause >= PSP_HW_TEXTURE_INVALIDATED_KEY) && (sample->cause <= PSP_HW_TEXTURE_CHANGED_VARIANT)) {
            sPspHwTextureFrame[PSP_HW_TEXTURE_FRAME_INVALIDATED + sample->cause - PSP_HW_TEXTURE_INVALIDATED_KEY]++;
        }
        // Evictions during reservation may have replaced this history slot
        if ((sPspHwTextureHistory[sample->history].key.pixels == request->pixels) &&
            (sPspHwTextureHistory[sample->history].key.format == request->format) &&
            (psp_hw_texture_changes(&sPspHwTextureHistory[sample->history].key, request) == 0)) {
            sPspHwTextureHistory[sample->history].state = PSP_HW_TEXTURE_REPEAT_KEY;
        }
    }
    sample->active = 0;
}

void PspHwCounterProfile_TextureRetire(const PspGfxTextureRequest* request, PspHwTextureRetireReason reason,
                                     u32 bytes, int retired, int edram) {
    PspHwTextureCause cause;
    u32 changes;
    u32 index;
    u64* totals;

    if (!sPspHwFrameArmed || ((u32) request->format >= PSP_HW_TEXTURE_FORMAT_COUNT)) {
        return;
    }
    index = psp_hw_texture_history(request, &cause, &changes);
    if ((reason == PSP_HW_TEXTURE_INVALIDATE) || (cause == PSP_HW_TEXTURE_INVALIDATED_KEY)) {
        sPspHwTextureHistory[index].state = PSP_HW_TEXTURE_INVALIDATED_KEY;
    } else {
        sPspHwTextureHistory[index].state = PSP_HW_TEXTURE_EVICTED_KEY;
    }
    totals = sPspHwTextureRetires[request->format][reason];
    totals[0]++;
    totals[1] += bytes;
    totals[2] += retired != 0;
    totals[3] += edram != 0;
}

static int psp_hw_texture_dump(SceUID fd) {
    char line[512];
    u32 format;
    u32 cause;
    u32 i;
    u32 j;
    u32 length;

    snprintf(line, sizeof(line), "\n[texture attribution]\nmetric,value\nversion,1\nhistory_slots,%u\n"
             "history_replacements,%lu\nsource_slots,%u\nsource_overflow_attempts,%llu\n",
             PSP_HW_TEXTURE_HISTORY_COUNT, (unsigned long) sPspHwTextureHistoryReplacements,
             PSP_HW_TEXTURE_SOURCE_COUNT,
             (unsigned long long) sPspHwTextureSources[PSP_HW_TEXTURE_SOURCE_COUNT].metric[PSP_HW_TEXTURE_ATTEMPTS]);
    if (!psp_hw_write_all(fd, line) || !psp_hw_write_all(fd, "\n[texture creates]\nformat,cause")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_TEXTURE_METRIC_COUNT; i++) {
        snprintf(line, sizeof(line), ",%s", sPspHwTextureMetricNames[i]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }
    for (format = 0; format < PSP_HW_TEXTURE_FORMAT_COUNT; format++) {
        for (cause = 0; cause < PSP_HW_TEXTURE_CAUSE_COUNT; cause++) {
            length = snprintf(line, sizeof(line), "%s,%s", sPspHwTextureFormatNames[format], sPspHwTextureCauseNames[cause]);
            for (i = 0; i < PSP_HW_TEXTURE_METRIC_COUNT; i++) {
                length += snprintf(line + length, sizeof(line) - length, ",%llu",
                                   (unsigned long long) sPspHwTextureCreates[format][cause][i]);
            }
            line[length++] = '\n';
            line[length] = 0;
            if (!psp_hw_write_all(fd, line)) {
                return 0;
            }
        }
    }
    if (!psp_hw_write_all(fd, "\n[texture retirements]\nformat,reason,entries,bytes,already_retired,edram_entries\n")) {
        return 0;
    }
    for (format = 0; format < PSP_HW_TEXTURE_FORMAT_COUNT; format++) {
        for (i = 0; i < PSP_HW_TEXTURE_RETIRE_COUNT; i++) {
            const u64* totals = sPspHwTextureRetires[format][i];
            snprintf(line, sizeof(line), "%s,%s,%llu,%llu,%llu,%llu\n", sPspHwTextureFormatNames[format],
                     sPspHwTextureRetireNames[i], (unsigned long long) totals[0], (unsigned long long) totals[1],
                     (unsigned long long) totals[2], (unsigned long long) totals[3]);
            if (!psp_hw_write_all(fd, line)) {
                return 0;
            }
        }
    }
    if (!psp_hw_write_all(fd, "\n[texture sources]\nsource,format")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_TEXTURE_METRIC_COUNT; i++) {
        snprintf(line, sizeof(line), ",%s", sPspHwTextureMetricNames[i]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    for (i = 0; i < PSP_HW_TEXTURE_CAUSE_COUNT; i++) {
        snprintf(line, sizeof(line), ",%s", sPspHwTextureCauseNames[i]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }
    for (i = 0; i <= sPspHwTextureSourceCount; i++) {
        const PspHwTextureSource* source = &sPspHwTextureSources[i];
        if (source->metric[PSP_HW_TEXTURE_ATTEMPTS] == 0) {
            continue;
        }
        length = snprintf(line, sizeof(line), "0x%08lx,%s", (unsigned long) source->pixels,
                          i == PSP_HW_TEXTURE_SOURCE_COUNT ? "overflow" : sPspHwTextureFormatNames[source->format]);
        for (j = 0; j < PSP_HW_TEXTURE_METRIC_COUNT; j++) {
            length += snprintf(line + length, sizeof(line) - length, ",%llu", (unsigned long long) source->metric[j]);
        }
        for (j = 0; j < PSP_HW_TEXTURE_CAUSE_COUNT; j++) {
            length += snprintf(line + length, sizeof(line) - length, ",%lu", (unsigned long) source->cause[j]);
        }
        line[length++] = '\n';
        line[length] = 0;
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[texture variant changes]\nsource,format,field,uploads\n")) {
        return 0;
    }
    for (i = 0; i <= sPspHwTextureSourceCount; i++) {
        const PspHwTextureSource* source = &sPspHwTextureSources[i];
        for (j = 0; j < PSP_HW_TEXTURE_CHANGE_COUNT; j++) {
            if (source->changes[j] == 0) {
                continue;
            }
            snprintf(line, sizeof(line), "0x%08lx,%s,%s,%lu\n", (unsigned long) source->pixels,
                     i == PSP_HW_TEXTURE_SOURCE_COUNT ? "overflow" : sPspHwTextureFormatNames[source->format],
                     sPspHwTextureChangeNames[j], (unsigned long) source->changes[j]);
            if (!psp_hw_write_all(fd, line)) {
                return 0;
            }
        }
    }
    if (!psp_hw_write_all(fd, "\n[texture frames]\nframe")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_TEXTURE_FRAME_COUNT; i++) {
        snprintf(line, sizeof(line), ",%s", sPspHwTextureFrameNames[i]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }
    for (i = 0; i < sPspHwTotals.frames; i++) {
        length = snprintf(line, sizeof(line), "%lu", (unsigned long) i);
        for (j = 0; j < PSP_HW_TEXTURE_FRAME_COUNT; j++) {
            length += snprintf(line + length, sizeof(line) - length, ",%lu", (unsigned long) sPspHwFrames[i].texture[j]);
        }
        line[length++] = '\n';
        line[length] = 0;
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}
