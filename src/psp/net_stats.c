#include <stdio.h>
#include <string.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_fcntl.h>
#include "src/psp/net_stats.h"
#include "src/psp/adhoc.h"
#include "src/psp/settings_path.h"

static PspNetStats sStats;
static volatile int sActive, sAvailable;
static u32 sStarted, sStopped, sPreviousStep, sLastReceived, sLastRead;
static int sStepValid, sPathValid = 1;
typedef struct {
    u32 epoch, frame;
    unsigned fractionQ8;
} PspWatchPose;
static PspWatchPose sBuiltPose, sLastPose;
static struct {
    const void* task;
    PspWatchPose pose;
} sTaskPoses[GFX_TASK_POOL_COUNT];
static u32 sLastCaptureUs, sLastReceiveUs, sLastWatchPresentUs, sWatchHold;
static int sCaptureValid;
static char sPath[256] = "sf64-adhoc-stats.csv";
static const char* sSaveStatus = "No report saved";

static const char* const sCounterNames[] = {
    "capture_attempts", "oversize_captures", "published_worlds", "pending_replacements",
    "tx_peer_worlds", "tx_fragments", "tx_payload_bytes", "tx_packet_bytes", "tx_would_block",
    "rx_complete_worlds", "rx_fragments", "rx_payload_bytes", "rx_packet_bytes", "rx_duplicate_fragments",
    "rx_old_fragments", "rx_invalid_fragments", "rx_abandoned_worlds", "rx_checksum_failures",
    "rx_sequence_gaps", "game_reads", "game_read_sequence_gaps", "game_rejected_worlds", "presented_frames",
    "native_capture_bytes", "packed_captures", "native_captures"
};
static const char* const sTimingNames[] = {
    "guard_publish", "guard_send_copy", "guard_receive_promote", "guard_game_read",
    "capture_build", "game_validate", "game_apply", "game_update", "adhoc_service", "service_interval",
    "render_through_submit", "present", "capture_interval", "receive_interval", "watch_present_interval"
};
static const char* const sPartNames[] = { "header", "players", "shots", "actors", "items", "effects", "scenery" };

u32 PspNetStats_NowUs(void) { return sceKernelGetSystemTimeLow(); }
int PspNetStats_Active(void) { return sActive; }
int PspNetStats_Available(void) { return sAvailable; }

void PspNetStats_Init(const char* executablePath) {
    sPathValid = executablePath ? PspSettingsPath_FromExecutable(sPath, sizeof(sPath), executablePath,
                                                               "sf64-adhoc-stats.csv") : 1;
}

void PspNetStats_Reset(void) {
    int lock = sceKernelCpuSuspendIntr();
    unsigned i;
    int role = sStats.role;
    volatile u8* data = (volatile u8*) &sStats;
    for (i = 0; i < sizeof(sStats); i++) data[i] = 0;
    sStats.role = role;
    sStarted = sStopped = PspNetStats_NowUs();
    sLastReceived = sLastRead = 0;
    sStepValid = sCaptureValid = 0;
    sLastPose.epoch = sBuiltPose.epoch = 0;
    sTaskPoses[0].task = sTaskPoses[1].task = NULL;
    sWatchHold = 0;
    sAvailable = 1;
    sSaveStatus = "Counters reset";
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Begin(int host) {
    PspNetStats_Reset();
    sStats.role = host ? 1 : 2;
    sActive = 1;
}

void PspNetStats_End(void) {
    int lock = sceKernelCpuSuspendIntr();
    if (sActive) sStopped = PspNetStats_NowUs();
    sActive = 0;
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_AddLocked(PspNetCounter counter, u32 amount) {
    if (sActive && counter >= 0 && counter < PSP_NET_COUNTER_COUNT) sStats.counters[counter] += amount;
}

void PspNetStats_TimeLocked(PspNetTimingKind kind, u32 elapsedUs) {
    PspNetTiming* time;
    if (!sActive || kind < 0 || kind >= PSP_NET_TIMING_COUNT) return;
    time = &sStats.timings[kind];
    time->count++;
    time->totalUs += elapsedUs;
    if (elapsedUs > time->maxUs) time->maxUs = elapsedUs;
}

void PspNetStats_Add(PspNetCounter counter, u32 amount) {
    int lock;
    if (!sActive) return;
    lock = sceKernelCpuSuspendIntr();
    PspNetStats_AddLocked(counter, amount);
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Time(PspNetTimingKind kind, u32 startedUs) {
    u32 elapsed = PspNetStats_NowUs() - startedUs;
    int lock;
    if (!sActive) return;
    lock = sceKernelCpuSuspendIntr();
    PspNetStats_TimeLocked(kind, elapsed);
    sceKernelCpuResumeIntr(lock);
}

static const unsigned sCadenceLimits[] = { 25000, 42000, 58000, 75000, 92000, 110000, 150000 };

static void stats_cadence(PspNetTimingKind kind, u32 elapsedUs) {
    unsigned bin = 0;
    while (bin < 7 && elapsedUs > sCadenceLimits[bin]) bin++;
    sStats.cadenceBins[kind - PSP_NET_CAPTURE_INTERVAL][bin]++;
    PspNetStats_TimeLocked(kind, elapsedUs);
}

void PspNetStats_SetWatchPose(u32 epoch, u32 frame, unsigned fractionQ8) {
    sBuiltPose = (PspWatchPose) { epoch, frame, fractionQ8 };
}

void PspNetStats_TagTask(int slot, const void* task) {
    int lock;
    if (!sActive || sStats.role != 2) return;
    lock = sceKernelCpuSuspendIntr();
    sTaskPoses[slot].task = task;
    sTaskPoses[slot].pose = sBuiltPose;
    sBuiltPose.epoch = 0;
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Presented(const void* task, u32 nowUs) {
    int lock = sceKernelCpuSuspendIntr();
    PspWatchPose pose = { 0 };
    int slot;
    PspNetStats_AddLocked(PSP_NET_PRESENTED, 1);
    if (sActive && sStats.role == 2) {
        for (slot = 0; slot < GFX_TASK_POOL_COUNT; slot++) {
            if (sTaskPoses[slot].task == task) {
                pose = sTaskPoses[slot].pose;
                break;
            }
        }
        if (pose.epoch && pose.epoch == sLastPose.epoch) {
            s64 advance = (s64) (s32) (pose.frame - sLastPose.frame) * 256 +
                          (int) pose.fractionQ8 - (int) sLastPose.fractionQ8;
            unsigned bin;
            stats_cadence(PSP_NET_WATCH_PRESENT_INTERVAL, nowUs - sLastWatchPresentUs);
            if (advance < 0) bin = 0;
            else if (advance == 0) bin = 1;
            else if (advance < 192) bin = 2;
            else if (advance <= 320) bin = 3;
            else if (advance < 512) bin = 4;
            else bin = 5;
            sStats.playbackBins[bin]++;
            if (advance > sStats.playbackMaxAdvanceQ8)
                sStats.playbackMaxAdvanceQ8 = advance > 0xffffffffLL ? 0xffffffffU : (u32) advance;
            if (advance == 0) {
                sWatchHold++;
                if (sWatchHold > sStats.playbackMaxHold) sStats.playbackMaxHold = sWatchHold;
            } else sWatchHold = 0;
        } else sWatchHold = 0;
        sLastPose = pose;
        sLastWatchPresentUs = nowUs;
    }
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Capture(int bytes, const u32 parts[PSP_NET_PART_COUNT], u32 startedUs) {
    u32 elapsed = PspNetStats_NowUs() - startedUs;
    unsigned bin, i;
    int lock = sceKernelCpuSuspendIntr();
    PspNetStats_AddLocked(PSP_NET_CAPTURE, 1);
    PspNetStats_TimeLocked(PSP_NET_BUILD, elapsed);
    if (bytes <= 0) PspNetStats_AddLocked(PSP_NET_OVERSIZE, 1);
    else if (sActive) {
        if (sCaptureValid) stats_cadence(PSP_NET_CAPTURE_INTERVAL, startedUs - sLastCaptureUs);
        sCaptureValid = 1;
        sLastCaptureUs = startedUs;
        bin = ((u32) bytes - 1) / 4096;
        if (bin > 7) bin = 7;
        sStats.sizeBins[bin]++;
        sStats.lastBytes = bytes;
        if ((u32) bytes > sStats.maxBytes) sStats.maxBytes = bytes;
        if (((u32) bytes + 1023) / 1024 > sStats.maxFragments) sStats.maxFragments = ((u32) bytes + 1023) / 1024;
        for (i = 0; i < PSP_NET_PART_COUNT; i++) sStats.payload[i] += parts[i];
    }
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Transmit(int peer, const u8* mac, int bytes, u32 queuedUs, int complete, int blocked) {
    PspNetPeerStats* stats;
    u32 elapsed = PspNetStats_NowUs() - queuedUs;
    int lock = sceKernelCpuSuspendIntr();
    if (sActive && peer >= 0 && peer < 3) {
        stats = &sStats.peers[peer];
        if (memcmp(stats->mac, mac, 6)) {
            memset(stats, 0, sizeof(*stats));
            memcpy(stats->mac, mac, 6);
        }
        if (blocked) {
            PspNetStats_AddLocked(PSP_NET_TX_BLOCK, 1);
            stats->blocked++;
        } else {
            PspNetStats_AddLocked(PSP_NET_TX_FRAGMENT, 1);
            PspNetStats_AddLocked(PSP_NET_TX_PAYLOAD, bytes);
            PspNetStats_AddLocked(PSP_NET_TX_PACKET, bytes + 32);
            stats->fragments++;
            stats->payloadBytes += bytes;
            if (complete) {
                PspNetStats_AddLocked(PSP_NET_TX_WORLD, 1);
                stats->worlds++;
                stats->latency.count++;
                stats->latency.totalUs += elapsed;
                if (elapsed > sStats.maxQueueAgeUs) sStats.maxQueueAgeUs = elapsed;
                if (elapsed > stats->latency.maxUs) stats->latency.maxUs = elapsed;
            }
        }
    }
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_ReceivedLocked(u32 sequence) {
    PspNetStats_AddLocked(PSP_NET_RX_WORLD, 1);
    if (sActive) {
        u32 nowUs = PspNetStats_NowUs();
        if (sLastReceived) stats_cadence(PSP_NET_RECEIVE_INTERVAL, nowUs - sLastReceiveUs);
        sLastReceiveUs = nowUs;
        if (sLastReceived && (s32) (sequence - sLastReceived) > 1)
            PspNetStats_AddLocked(PSP_NET_RX_GAPS, sequence - sLastReceived - 1);
        sLastReceived = sequence;
    }
}

void PspNetStats_Received(u32 sequence) {
    int lock = sceKernelCpuSuspendIntr();
    PspNetStats_ReceivedLocked(sequence);
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Fragment(int bytes, int duplicate) {
    int lock = sceKernelCpuSuspendIntr();
    PspNetStats_AddLocked(PSP_NET_RX_FRAGMENT, 1);
    PspNetStats_AddLocked(PSP_NET_RX_PAYLOAD, bytes);
    if (duplicate) PspNetStats_AddLocked(PSP_NET_RX_DUPLICATE, 1);
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_ReadLocked(u32 sequence) {
    PspNetStats_AddLocked(PSP_NET_READ, 1);
    if (sActive) {
        if (sLastRead && (s32) (sequence - sLastRead) > 1)
            PspNetStats_AddLocked(PSP_NET_READ_GAPS, sequence - sLastRead - 1);
        sLastRead = sequence;
    }
}

void PspNetStats_QueueLocked(u32 activeSequence, u32 pendingSequence, int activeBytes,
                           int pendingBytes, int offset, int peer, u32 queuedUs) {
    if (!sActive) return;
    sStats.activeSequence = activeBytes ? activeSequence : 0;
    sStats.pendingSequence = pendingSequence != activeSequence ? pendingSequence : 0;
    sStats.activeBytes = activeBytes;
    sStats.pendingBytes = sStats.pendingSequence ? pendingBytes : 0;
    sStats.offset = offset;
    sStats.peer = peer;
    sStats.queueAgeUs = activeBytes ? PspNetStats_NowUs() - queuedUs : 0;
    if (sStats.queueAgeUs > sStats.maxQueueAgeUs) sStats.maxQueueAgeUs = sStats.queueAgeUs;
}

void PspNetStats_Service(u32 startedUs) {
    int lock = sceKernelCpuSuspendIntr();
    PspNetStats_TimeLocked(PSP_NET_SERVICE, PspNetStats_NowUs() - startedUs);
    if (sStepValid) PspNetStats_TimeLocked(PSP_NET_STEP_INTERVAL, startedUs - sPreviousStep);
    sPreviousStep = startedUs;
    sStepValid = 1;
    sceKernelCpuResumeIntr(lock);
}

void PspNetStats_Get(PspNetStats* out) {
    int lock = sceKernelCpuSuspendIntr();
    unsigned i;
    const volatile u8* source = (const volatile u8*) &sStats;
    u8* target = (u8*) out;
    for (i = 0; i < sizeof(*out); i++) target[i] = source[i];
    out->active = sActive;
    out->elapsedUs = (sActive ? PspNetStats_NowUs() : sStopped) - sStarted;
    sceKernelCpuResumeIntr(lock);
}

static int stats_write(int file, const char* line) {
    int bytes = strlen(line), written;
    while (bytes) {
        written = sceIoWrite(file, line, bytes);
        if (written <= 0 || written > bytes) return 0;
        bytes -= written;
        line += written;
    }
    return 1;
}

int PspNetStats_Save(void) {
    static PspNetStats stats;
    PspAdhocView view;
    char line[256];
    int file, ok = 1, i;
    PspNetStats_Get(&stats);
    PspAdhoc_GetView(&view);
    if (!sPathValid || !sAvailable) { sSaveStatus = "No measurement or invalid report path"; return 0; }
    file = sceIoOpen(sPath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0666);
    if (file < 0) { sSaveStatus = "Could not open statistics report"; return 0; }
    snprintf(line, sizeof(line), "report,1,elapsed_us,%lu,active,%d,role,%d,state,%d,room,%d,peers,%d,channel,%d\n",
             (unsigned long) stats.elapsedUs, stats.active, stats.role, view.state, view.roomId, view.peerCount, view.channel);
    ok = stats_write(file, line);
    for (i = 0; i < PSP_NET_COUNTER_COUNT && ok; i++) {
        snprintf(line, sizeof(line), "counter,%s,%llu\n", sCounterNames[i], (unsigned long long) stats.counters[i]);
        ok = stats_write(file, line);
    }
    for (i = 0; i < PSP_NET_TIMING_COUNT && ok; i++) {
        snprintf(line, sizeof(line), "timing_us,%s,calls,%lu,total,%llu,max,%lu\n", sTimingNames[i],
                 (unsigned long) stats.timings[i].count, (unsigned long long) stats.timings[i].totalUs,
                 (unsigned long) stats.timings[i].maxUs);
        ok = stats_write(file, line);
    }
    for (i = 0; i < PSP_NET_PART_COUNT && ok; i++) {
        snprintf(line, sizeof(line), "payload_bytes,%s,%llu\n", sPartNames[i], (unsigned long long) stats.payload[i]);
        ok = stats_write(file, line);
    }
    for (i = 0; i < 8 && ok; i++) {
        snprintf(line, sizeof(line), "size_bin_le_bytes,%d,%lu\n", (i + 1) * 4096, (unsigned long) stats.sizeBins[i]);
        ok = stats_write(file, line);
    }
    for (i = 0; i < 3 && ok; i++) {
        const PspNetPeerStats* peer = &stats.peers[i];
        snprintf(line, sizeof(line), "peer,%d,%02X%02X%02X%02X%02X%02X,worlds,%lu,fragments,%lu,payload,%llu,blocked,%lu,latency_total_us,%llu,latency_max_us,%lu\n",
                 i, peer->mac[0], peer->mac[1], peer->mac[2], peer->mac[3], peer->mac[4], peer->mac[5],
                 (unsigned long) peer->worlds, (unsigned long) peer->fragments, (unsigned long long) peer->payloadBytes,
                 (unsigned long) peer->blocked, (unsigned long long) peer->latency.totalUs, (unsigned long) peer->latency.maxUs);
        ok = stats_write(file, line);
    }
    for (i = 0; i < 3 && ok; i++) {
        unsigned bin;
        for (bin = 0; bin < 8 && ok; bin++) {
            snprintf(line, sizeof(line), "cadence_bin_us,%s,%u,%lu\n",
                     sTimingNames[PSP_NET_CAPTURE_INTERVAL + i], bin < 7 ? sCadenceLimits[bin] : 0,
                     (unsigned long) stats.cadenceBins[i][bin]);
            ok = stats_write(file, line);
        }
    }
    if (ok) {
        snprintf(line, sizeof(line),
                 "watch_playback,backward,%lu,hold,%lu,lt_0_75,%lu,0_75_to_1_25,%lu,gt_1_25_lt_2,%lu,ge_2,%lu,max_advance_q8,%lu,max_hold_run,%lu\n",
                 (unsigned long) stats.playbackBins[0], (unsigned long) stats.playbackBins[1],
                 (unsigned long) stats.playbackBins[2], (unsigned long) stats.playbackBins[3],
                 (unsigned long) stats.playbackBins[4], (unsigned long) stats.playbackBins[5],
                 (unsigned long) stats.playbackMaxAdvanceQ8, (unsigned long) stats.playbackMaxHold);
        ok = stats_write(file, line);
    }
    snprintf(line, sizeof(line), "queue,active_seq,%lu,pending_seq,%lu,active_bytes,%lu,pending_bytes,%lu,offset,%lu,peer,%lu,age_us,%lu,max_age_us,%lu\nend_report\n",
             (unsigned long) stats.activeSequence, (unsigned long) stats.pendingSequence, (unsigned long) stats.activeBytes,
             (unsigned long) stats.pendingBytes, (unsigned long) stats.offset, (unsigned long) stats.peer,
             (unsigned long) stats.queueAgeUs, (unsigned long) stats.maxQueueAgeUs);
    if (ok) ok = stats_write(file, line);
    if (sceIoClose(file) < 0) ok = 0;
    sSaveStatus = ok ? "Saved sf64-adhoc-stats.csv" : "Statistics report write failed";
    return ok;
}

const char* PspNetStats_SaveStatus(void) { return sSaveStatus; }
