#ifndef PSP_NET_STATS_H
#define PSP_NET_STATS_H

#include "PR/ultratypes.h"

typedef enum {
    PSP_NET_CAPTURE, PSP_NET_OVERSIZE, PSP_NET_PUBLISH, PSP_NET_REPLACED,
    PSP_NET_TX_WORLD, PSP_NET_TX_FRAGMENT, PSP_NET_TX_PAYLOAD, PSP_NET_TX_PACKET, PSP_NET_TX_BLOCK,
    PSP_NET_RX_WORLD, PSP_NET_RX_FRAGMENT, PSP_NET_RX_PAYLOAD, PSP_NET_RX_PACKET, PSP_NET_RX_DUPLICATE,
    PSP_NET_RX_OLD, PSP_NET_RX_INVALID, PSP_NET_RX_ABANDONED, PSP_NET_RX_CHECKSUM,
    PSP_NET_RX_GAPS, PSP_NET_READ, PSP_NET_READ_GAPS, PSP_NET_REJECTED, PSP_NET_PRESENTED,
    PSP_NET_CAPTURE_NATIVE_BYTES, PSP_NET_PACKED_CAPTURE, PSP_NET_NATIVE_CAPTURE, PSP_NET_COUNTER_COUNT
} PspNetCounter;

typedef enum {
    PSP_NET_GUARD_PUBLISH, PSP_NET_GUARD_SEND, PSP_NET_GUARD_RECEIVE, PSP_NET_GUARD_READ,
    PSP_NET_BUILD, PSP_NET_VALIDATE, PSP_NET_APPLY, PSP_NET_GAME, PSP_NET_SERVICE,
    PSP_NET_STEP_INTERVAL, PSP_NET_RENDER, PSP_NET_PRESENT, PSP_NET_TIMING_COUNT
} PspNetTimingKind;

typedef enum {
    PSP_NET_HEADER, PSP_NET_PLAYERS, PSP_NET_SHOTS, PSP_NET_ACTORS,
    PSP_NET_ITEMS, PSP_NET_EFFECTS, PSP_NET_SCENERY, PSP_NET_PART_COUNT
} PspNetPayloadPart;

typedef struct { u64 totalUs; u32 count, maxUs; } PspNetTiming;
typedef struct {
    u8 mac[6];
    u32 worlds, fragments, blocked;
    u64 payloadBytes;
    PspNetTiming latency;
} PspNetPeerStats;

typedef struct {
    u32 elapsedUs;
    int active, role;
    u64 counters[PSP_NET_COUNTER_COUNT];
    PspNetTiming timings[PSP_NET_TIMING_COUNT];
    u64 payload[PSP_NET_PART_COUNT];
    u32 sizeBins[8], lastBytes, maxBytes, maxFragments;
    u32 activeSequence, pendingSequence, activeBytes, pendingBytes, offset, peer;
    u32 queueAgeUs, maxQueueAgeUs;
    PspNetPeerStats peers[3];
} PspNetStats;

void PspNetStats_Init(const char* executablePath);
void PspNetStats_Begin(int host);
void PspNetStats_End(void);
void PspNetStats_Reset(void);
int PspNetStats_Active(void);
int PspNetStats_Available(void);
u32 PspNetStats_NowUs(void);
void PspNetStats_Add(PspNetCounter counter, u32 amount);
// The caller holds the interrupt guard for Locked updates
void PspNetStats_AddLocked(PspNetCounter counter, u32 amount);
void PspNetStats_TimeLocked(PspNetTimingKind kind, u32 elapsedUs);
void PspNetStats_Time(PspNetTimingKind kind, u32 startedUs);
void PspNetStats_Capture(int bytes, const u32 parts[PSP_NET_PART_COUNT], u32 startedUs);
void PspNetStats_Transmit(int peer, const u8* mac, int bytes, u32 queuedUs, int complete, int blocked);
void PspNetStats_Received(u32 sequence);
void PspNetStats_ReceivedLocked(u32 sequence);
void PspNetStats_Fragment(int bytes, int duplicate);
void PspNetStats_ReadLocked(u32 sequence);
void PspNetStats_QueueLocked(u32 activeSequence, u32 pendingSequence, int activeBytes,
                           int pendingBytes, int offset, int peer, u32 queuedUs);
void PspNetStats_Service(u32 startedUs);
void PspNetStats_Get(PspNetStats* out);
// Save reports only at an idle graphics boundary
int PspNetStats_Save(void);
const char* PspNetStats_SaveStatus(void);

#endif
