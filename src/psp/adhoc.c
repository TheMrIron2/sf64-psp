#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>

#include "src/psp/adhoc.h"

#define ADHOC_PORT 6464
#define ADHOC_PACKET_BYTES 24
#define ADHOC_PROGRESS_BYTES 28
#define ADHOC_WORLD_HEADER 32
#define ADHOC_WORLD_CHUNK 1024
#define ADHOC_INTERVAL_US 500000U
#define ADHOC_PEER_TIMEOUT_US 3000000U
#define ADHOC_SEARCH_US 10000000U
#define ADHOC_CONNECT_US 12000000U
#define ADHOC_JOIN_US 5000000U
#define ADHOC_WOULD_BLOCK 0x80410709U

enum { REQUEST_STOP, REQUEST_HOST, REQUEST_SEARCH, REQUEST_JOIN, REQUEST_EXIT };
enum { PACKET_ADVERT = 1, PACKET_JOIN, PACKET_ACCEPT, PACKET_LEAVE, PACKET_CLOSE, PACKET_REJECT, PACKET_PROGRESS, PACKET_WORLD };

typedef struct {
    unsigned serial;
    int action;
    int matchType;
    PspAdhocRoom room;
} AdhocRequest;

typedef struct {
    u8 mac[6];
    u32 token;
    u32 sequence;
    u32 lastSeen;
} AdhocPeer;

typedef struct {
    int common;
    int module;
    int net;
    int adhoc;
    int control;
    int socket;
    int handler;
} AdhocResources;

static SceUID sThread = -1;
static AdhocRequest sRequest;
static PspAdhocView sPublished;
static PspAdhocView sView;
static AdhocResources sResources = { 0, 0, 0, 0, 0, -1, -1 };
static AdhocPeer sPeers[3];
static unsigned char sMac[8];
static unsigned char sReceive[8192];
static unsigned sSerial;
static int sHost;
static int sMatchType;
static u32 sSession;
static u32 sToken;
static u32 sSequence;
static u32 sAcceptedSequence;
static u32 sStarted;
static u32 sSent;
static u32 sReceived;
static PspAdhocRoom sTarget;
static unsigned sControlEvents;
static int sControlError;
static int sScanPending;
static int sJoinPending;
static u32 sScanStarted;
static u32 sLastScan;
static char sGroup[9];
static struct SceNetAdhocctlScanInfo sScanResults[32];
static struct {
    PspAdhocHostPhase phase;
    int stage;
    u32 frame;
    u32 sequence;
} sProgress;
static u32 sProgressReceived;

static u8 sWorldPending[PSP_ADHOC_WORLD_MAX_BYTES];
static u8 sWorldSending[PSP_ADHOC_WORLD_MAX_BYTES];
static u8 sWorldAssembly[PSP_ADHOC_WORLD_MAX_BYTES];
static u8 sWorldPublished[PSP_ADHOC_WORLD_MAX_BYTES];
static int sWorldPendingBytes, sWorldSendingBytes, sWorldPublishedBytes;
static u32 sWorldPendingSequence, sWorldSendingSequence, sWorldPublishedSequence;
static u32 sWorldAssemblySequence, sWorldAssemblyChecksum, sWorldAssemblyMask;
static int sWorldAssemblyBytes;
static int sWorldOffset, sWorldPeer;

static u32 adhoc_nonce(void);

static int adhoc_has_mac(const u8* mac) {
    return mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5];
}

static void adhoc_control_event(int event, int error, void* arg) {
    int lock = sceKernelCpuSuspendIntr();
    (void) arg;
    if (event >= 0 && event <= 4) sControlEvents |= 1U << event;
    if (event == 0) sControlError = error;
    sceKernelCpuResumeIntr(lock);
}

static void adhoc_clear_events(void) {
    int lock = sceKernelCpuSuspendIntr();
    sControlEvents = 0;
    sControlError = 0;
    sceKernelCpuResumeIntr(lock);
}

static u32 adhoc_read_u32(const u8* bytes) {
    return ((u32) bytes[0] << 24) | ((u32) bytes[1] << 16) | ((u32) bytes[2] << 8) | bytes[3];
}

static void adhoc_write_u32(u8* bytes, u32 value) {
    bytes[0] = value >> 24;
    bytes[1] = value >> 16;
    bytes[2] = value >> 8;
    bytes[3] = value;
}

static int adhoc_cancelled(void) {
    int lock = sceKernelCpuSuspendIntr();
    int cancelled = sSerial != sRequest.serial;
    sceKernelCpuResumeIntr(lock);
    return cancelled;
}

static void adhoc_publish(void) {
    int lock = sceKernelCpuSuspendIntr();
    if (sSerial == sRequest.serial) sPublished = sView;
    sceKernelCpuResumeIntr(lock);
}

static void adhoc_status(PspAdhocState state, const char* status) {
    sView.state = state;
    snprintf(sView.status, sizeof(sView.status), "%s", status);
    adhoc_publish();
}

static void adhoc_error(const char* step, int error) {
    sView.state = PSP_ADHOC_ERROR;
    snprintf(sView.status, sizeof(sView.status), "%s: %08lX (%luK free)", step,
             (unsigned long) (u32) error, (unsigned long) sceKernelMaxFreeMemSize() / 1024);
    adhoc_publish();
}

static int adhoc_send(const u8* mac, int type, u32 token, u32 sequence) {
    u8 packet[ADHOC_PROGRESS_BYTES] = { 'S', 'F', 'A', 'L', 1 };
    int result, length = ADHOC_PACKET_BYTES;

    packet[5] = type;
    packet[6] = sMatchType;
    packet[7] = sView.roomId;
    adhoc_write_u32(packet + 8, sSession);
    adhoc_write_u32(packet + 12, token);
    adhoc_write_u32(packet + 16, sequence);
    if (type == PACKET_PROGRESS) {
        int lock = sceKernelCpuSuspendIntr();
        packet[20] = sProgress.phase;
        packet[21] = sProgress.stage;
        adhoc_write_u32(packet + 16, sProgress.sequence);
        adhoc_write_u32(packet + 24, sProgress.frame);
        sceKernelCpuResumeIntr(lock);
        length = ADHOC_PROGRESS_BYTES;
    }
    result = sceNetAdhocPdpSend(sResources.socket, (unsigned char*) mac, ADHOC_PORT,
                               packet, length, 0, 1);
    if (result < 0 && (u32) result != ADHOC_WOULD_BLOCK) {
        adhoc_error("Send", result);
        return 0;
    }
    return 1;
}

static u32 adhoc_world_checksum(const u8* data, int bytes) {
    u32 hash = 2166136261U;
    int i;
    for (i = 0; i < bytes; i++) hash = (hash ^ data[i]) * 16777619U;
    return hash;
}

static void adhoc_world_send(void) {
    static u8 packet[ADHOC_WORLD_HEADER + ADHOC_WORLD_CHUNK];
    static u32 checksum;
    int budget, lock, bytes, result;

    if (!sHost || sView.state != PSP_ADHOC_HOSTING || !sView.peerCount) return;
    if (!sWorldSendingBytes) {
        lock = sceKernelCpuSuspendIntr();
        if (sWorldPendingSequence != sWorldSendingSequence && sWorldPendingBytes) {
            sWorldSendingBytes = sWorldPendingBytes;
            sWorldSendingSequence = sWorldPendingSequence;
            memcpy(sWorldSending, sWorldPending, sWorldSendingBytes);
        }
        sceKernelCpuResumeIntr(lock);
        if (!sWorldSendingBytes) return;
        checksum = adhoc_world_checksum(sWorldSending, sWorldSendingBytes);
        sWorldOffset = sWorldPeer = 0;
    }
    for (budget = 0; budget < 4 && sWorldPeer < 3; ) {
        if (adhoc_cancelled()) return;
        if (!sPeers[sWorldPeer].token) { sWorldPeer++; sWorldOffset = 0; continue; }
        bytes = sWorldSendingBytes - sWorldOffset;
        if (bytes > ADHOC_WORLD_CHUNK) bytes = ADHOC_WORLD_CHUNK;
        memcpy(packet, "SFAL", 4);
        packet[4] = 1; packet[5] = PACKET_WORLD; packet[6] = sMatchType; packet[7] = sView.roomId;
        adhoc_write_u32(packet + 8, sSession);
        adhoc_write_u32(packet + 12, sPeers[sWorldPeer].token);
        adhoc_write_u32(packet + 16, sWorldSendingSequence);
        adhoc_write_u32(packet + 20, sWorldSendingBytes);
        adhoc_write_u32(packet + 24, sWorldOffset);
        adhoc_write_u32(packet + 28, checksum);
        memcpy(packet + ADHOC_WORLD_HEADER, sWorldSending + sWorldOffset, bytes);
        result = sceNetAdhocPdpSend(sResources.socket, sPeers[sWorldPeer].mac, ADHOC_PORT,
                                   packet, bytes + ADHOC_WORLD_HEADER, 0, 1);
        if ((u32) result == ADHOC_WOULD_BLOCK) return;
        if (result < 0) { adhoc_error("World send", result); return; }
        budget++;
        sWorldOffset += bytes;
        if (sWorldOffset == sWorldSendingBytes) { sWorldPeer++; sWorldOffset = 0; }
    }
    if (sWorldPeer == 3) sWorldSendingBytes = 0;
}

static void adhoc_world_receive(const u8* packet, int length) {
    u32 sequence = adhoc_read_u32(packet + 16);
    u32 bytes = adhoc_read_u32(packet + 20);
    u32 offset = adhoc_read_u32(packet + 24);
    u32 checksum = adhoc_read_u32(packet + 28);
    u32 chunks, mask;
    int payload = length - ADHOC_WORLD_HEADER;
    int lock;

    if (!sequence || bytes == 0 || bytes > PSP_ADHOC_WORLD_MAX_BYTES || offset >= bytes ||
        offset % ADHOC_WORLD_CHUNK || payload <= 0 || payload > ADHOC_WORLD_CHUNK ||
        (u32) payload != (bytes - offset > ADHOC_WORLD_CHUNK ? ADHOC_WORLD_CHUNK : bytes - offset) ||
        (s32) (sequence - sWorldPublishedSequence) <= 0) return;
    if (sequence != sWorldAssemblySequence) {
        if (sWorldAssemblySequence && (s32) (sequence - sWorldAssemblySequence) <= 0) return;
        sWorldAssemblySequence = sequence;
        sWorldAssemblyBytes = bytes;
        sWorldAssemblyChecksum = checksum;
        sWorldAssemblyMask = 0;
    }
    if (bytes != (u32) sWorldAssemblyBytes || checksum != sWorldAssemblyChecksum) return;
    memcpy(sWorldAssembly + offset, packet + ADHOC_WORLD_HEADER, payload);
    sWorldAssemblyMask |= 1U << (offset / ADHOC_WORLD_CHUNK);
    chunks = (bytes + ADHOC_WORLD_CHUNK - 1) / ADHOC_WORLD_CHUNK;
    mask = chunks == 32 ? 0xffffffffU : (1U << chunks) - 1;
    if (sWorldAssemblyMask != mask || adhoc_world_checksum(sWorldAssembly, bytes) != checksum) return;
    lock = sceKernelCpuSuspendIntr();
    if (sSerial == sRequest.serial) {
        memcpy(sWorldPublished, sWorldAssembly, bytes);
        sWorldPublishedBytes = bytes;
        sWorldPublishedSequence = sequence;
        sView.worldSnapshots++;
        sView.worldBytes = bytes;
    }
    sceKernelCpuResumeIntr(lock);
}

static int adhoc_cleanup(void) {
    int result;
    if (sResources.socket >= 0) {
        result = sceNetAdhocPdpDelete(sResources.socket, 0);
        if (result < 0) { adhoc_error("Socket close", result); return 0; }
        sResources.socket = -1;
    }
    if (sResources.handler >= 0) {
        result = sceNetAdhocctlDelHandler(sResources.handler);
        if (result < 0) { adhoc_error("Handler close", result); return 0; }
        sResources.handler = -1;
    }
    if (sResources.control) {
        sceNetAdhocctlDisconnect();
        result = sceNetAdhocctlTerm();
        if (result < 0) { adhoc_error("Control close", result); return 0; }
        sResources.control = 0;
        sceKernelDelayThread(100000);
    }
    if (sResources.adhoc) {
        result = sceNetAdhocTerm();
        if (result < 0) { adhoc_error("Ad-Hoc close", result); return 0; }
        sResources.adhoc = 0;
    }
    if (sResources.net) {
        result = sceNetTerm();
        if (result < 0) { adhoc_error("Net close", result); return 0; }
        sResources.net = 0;
    }
    if (sResources.module) {
        result = sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
        if (result < 0) { adhoc_error("Module close", result); return 0; }
        sResources.module = 0;
    }
    if (sResources.common) {
        result = sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
        if (result < 0) { adhoc_error("COMMON close", result); return 0; }
        sResources.common = 0;
    }
    return 1;
}

static int adhoc_load(int module, int* owned, const char* name) {
    int result = sceUtilityLoadNetModule(module);
    if (result == 0) *owned = 1;
    else if ((u32) result != 0x80110802U && (u32) result != 0x80111102U) {
        adhoc_error(name, result);
        return 0;
    }
    return !adhoc_cancelled();
}

static int adhoc_open(void) {
    struct productStruct product;
    int result;

    if (sceWlanGetSwitchState() <= 0) {
        adhoc_status(PSP_ADHOC_ERROR, "Turn on the WLAN switch and retry");
        return 0;
    }
    adhoc_status(PSP_ADHOC_STARTING, "Loading network modules...");
    if (!adhoc_load(PSP_NET_MODULE_COMMON, &sResources.common, "COMMON")) return 0;
    if (!adhoc_load(PSP_NET_MODULE_ADHOC, &sResources.module, "ADHOC")) return 0;
    result = sceNetInit(0x20000, 0x20, 0x1000, 0x20, 0x1000);
    if (result < 0) { adhoc_error("Net init", result); return 0; }
    sResources.net = 1;
    if (adhoc_cancelled()) return 0;
    result = sceNetAdhocInit();
    if (result < 0) { adhoc_error("Ad-Hoc init", result); return 0; }
    sResources.adhoc = 1;
    if (adhoc_cancelled()) return 0;
    memset(&product, 0, sizeof(product));
    memcpy(product.product, "SF64PSP01", sizeof(product.product));
    result = sceNetAdhocctlInit(0x2000, 0x20, &product);
    if (result < 0) { adhoc_error("Control init", result); return 0; }
    sResources.control = 1;
    if (adhoc_cancelled()) return 0;
    result = sceNetAdhocctlAddHandler(adhoc_control_event, NULL);
    if (result < 0) { adhoc_error("Control handler", result); return 0; }
    sResources.handler = result;
    if (adhoc_cancelled()) return 0;
    result = sceWlanGetEtherAddr(sMac);
    if (result < 0) { adhoc_error("WLAN address", result); return 0; }
    if (adhoc_cancelled()) return 0;
    sStarted = sceKernelGetSystemTimeLow();
    sScanPending = sJoinPending = 0;
    adhoc_clear_events();
    if (sHost) {
        sSession = adhoc_nonce() & 0xFFFFFU;
        if (!sSession) sSession = 1;
        sView.roomId = sSession % 100;
        snprintf(sGroup, sizeof(sGroup), "SF%c%05lX", '0' + sMatchType, (unsigned long) (sSession & 0xFFFFFU));
        result = sceNetAdhocctlCreate(sGroup);
        if (result < 0) { adhoc_error("Create room", result); return 0; }
        adhoc_status(PSP_ADHOC_STARTING, "Creating host network...");
    } else {
        result = sceNetAdhocctlScan();
        if (result < 0) { adhoc_error("Radio scan", result); return 0; }
        sScanPending = 1;
        sScanStarted = sLastScan = sStarted;
        adhoc_status(PSP_ADHOC_SEARCHING, "Scanning nearby PSPs...");
    }
    return 1;
}

static u32 adhoc_nonce(void) {
    u32 value = sceKernelGetSystemTimeLow() ^ (sSerial * 0x9E3779B9U);
    int i;
    for (i = 0; i < 6; i++) value = (value ^ sMac[i]) * 16777619U;
    return value ? value : 1;
}

static int adhoc_same_room(const PspAdhocRoom* room, const u8* mac, u32 session) {
    return room->session == session && memcmp(room->mac, mac, 6) == 0;
}

static void adhoc_advert(const u8* mac, const u8* packet, u32 now) {
    (void) now;
    if (sHost || sView.state != PSP_ADHOC_JOINING ||
        adhoc_read_u32(packet + 8) != sTarget.session || packet[7] != sTarget.roomId) return;
    if (adhoc_has_mac(sTarget.mac) &&
        memcmp(sTarget.mac, mac, 6) != 0) return;
    memcpy(sTarget.mac, mac, 6);
}

static int adhoc_scan_room(const struct SceNetAdhocctlScanInfo* scan, PspAdhocRoom* room) {
    u32 session = 0;
    int i;
    if (scan->name[0] != 'S' || scan->name[1] != 'F' || scan->name[2] != '0' + sMatchType ||
        scan->unknown2 != 0 || (scan->channel != 1 && scan->channel != 6 && scan->channel != 11)) return 0;
    for (i = 3; i < 8; i++) {
        int digit = scan->name[i];
        if (digit >= '0' && digit <= '9') digit -= '0';
        else if (digit >= 'A' && digit <= 'F') digit -= 'A' - 10;
        else return 0;
        session = (session << 4) | digit;
    }
    if (!session || (scan->bssid[0] & 1) ||
        !(scan->bssid[0] | scan->bssid[1] | scan->bssid[2] | scan->bssid[3] | scan->bssid[4] | scan->bssid[5])) return 0;
    memset(room, 0, sizeof(*room));
    room->session = session;
    room->roomId = session % 100;
    room->matchType = sMatchType;
    room->channel = scan->channel;
    memcpy(room->group, scan->name, 8);
    memcpy(room->bssid, scan->bssid, 6);
    return 1;
}

static int adhoc_scan_results(void) {
    int length = sizeof(sScanResults), result, count, i;
    result = sceNetAdhocctlGetScanInfo(&length, sScanResults);
    if (result < 0) { adhoc_error("Scan results", result); return 0; }
    if (length < 0 || length > (int) sizeof(sScanResults) || length % sizeof(sScanResults[0])) {
        adhoc_status(PSP_ADHOC_ERROR, "Invalid radio scan results");
        return 0;
    }
    count = length / sizeof(sScanResults[0]);
    sView.scans++;
    sView.scanGroups = count;
    sView.roomCount = 0;
    for (i = 0; i < count && sView.roomCount < PSP_ADHOC_MAX_ROOMS; i++) {
        PspAdhocRoom room;
        int j;
        if (!adhoc_scan_room(&sScanResults[i], &room)) continue;
        for (j = 0; j < sView.roomCount; j++)
            if (memcmp(sView.rooms[j].bssid, room.bssid, 6) == 0 &&
                memcmp(sView.rooms[j].group, room.group, 8) == 0) break;
        if (j == sView.roomCount) sView.rooms[sView.roomCount++] = room;
    }
    return 1;
}

static void adhoc_begin_join(void) {
    struct SceNetAdhocctlScanInfo scan;
    int result;
    memset(&scan, 0, sizeof(scan));
    scan.channel = sTarget.channel;
    memcpy(scan.name, sTarget.group, 8);
    memcpy(scan.bssid, sTarget.bssid, 6);
    adhoc_clear_events();
    result = sceNetAdhocctlJoin(&scan);
    sJoinPending = 0;
    if (result < 0) { adhoc_error("Join network", result); return; }
    sStarted = sceKernelGetSystemTimeLow();
    adhoc_status(PSP_ADHOC_JOINING, "Joining host network...");
}

static void adhoc_host_packet(const u8* mac, const u8* packet, u32 now) {
    u32 token = adhoc_read_u32(packet + 12);
    u32 sequence = adhoc_read_u32(packet + 16);
    int i, empty = -1;

    for (i = 0; i < 3; i++) {
        if (!sPeers[i].token) empty = i;
        else if (memcmp(sPeers[i].mac, mac, 6) == 0) break;
    }
    if (packet[5] == PACKET_LEAVE) {
        if (i < 3 && sPeers[i].token == token) sPeers[i].token = 0;
        return;
    }
    if (packet[5] != PACKET_JOIN) return;
    if (i == 3) {
        i = empty;
        if (i >= 0) {
            memcpy(sPeers[i].mac, mac, 6);
            sPeers[i].token = token;
            sPeers[i].sequence = sequence;
            sPeers[i].lastSeen = now;
        }
    }
    if (i < 0 || sPeers[i].token != token) {
        adhoc_send(mac, PACKET_REJECT, token, sequence);
        return;
    }
    if ((s32) (sequence - sPeers[i].sequence) > 0) {
        sPeers[i].sequence = sequence;
        sPeers[i].lastSeen = now;
    }
    if (adhoc_send(mac, PACKET_ACCEPT, token, sequence))
        adhoc_send(mac, PACKET_PROGRESS, token, 0);
}

static void adhoc_packet(const u8* mac, unsigned short port, const u8* packet, int length, u32 now) {
    u32 session, token, sequence;
    if (length < ADHOC_PACKET_BYTES || port != ADHOC_PORT || (mac[0] & 1) ||
        !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) ||
        memcmp(mac, sMac, 6) == 0 || memcmp(packet, "SFAL", 4) != 0 || packet[4] != 1 ||
        packet[5] < PACKET_ADVERT || packet[5] > PACKET_WORLD || packet[6] != sMatchType ||
        packet[7] > 99) return;
    if (packet[5] == PACKET_WORLD) {
        if (length <= ADHOC_WORLD_HEADER || length > ADHOC_WORLD_HEADER + ADHOC_WORLD_CHUNK) return;
    } else if (packet[5] == PACKET_PROGRESS) {
        if (length != ADHOC_PROGRESS_BYTES || packet[20] > PSP_ADHOC_HOST_PAUSED ||
            packet[21] > 2 || packet[22] || packet[23]) return;
    } else if (length != ADHOC_PACKET_BYTES || adhoc_read_u32(packet + 20) != 0) return;
    session = adhoc_read_u32(packet + 8);
    token = adhoc_read_u32(packet + 12);
    sequence = adhoc_read_u32(packet + 16);
    if (!session) return;
    if (packet[5] == PACKET_ADVERT) {
        if (!token && !sequence) adhoc_advert(mac, packet, now);
        return;
    }
    if (!token || session != sSession) return;
    if (sHost) {
        adhoc_host_packet(mac, packet, now);
        return;
    }
    if ((sView.state != PSP_ADHOC_JOINING && sView.state != PSP_ADHOC_CONNECTED) ||
        !adhoc_same_room(&sTarget, mac, session) || token != sToken) return;
    if (packet[5] == PACKET_WORLD) {
        if (sView.state == PSP_ADHOC_CONNECTED) adhoc_world_receive(packet, length);
        return;
    }
    if (packet[5] == PACKET_PROGRESS) {
        if ((s32) (sequence - sProgressReceived) > 0) {
            sProgressReceived = sequence;
            sView.hostPhase = packet[20];
            sView.hostStage = packet[21];
            sView.hostFrame = adhoc_read_u32(packet + 24);
        }
        return;
    }
    if ((s32) (sequence - sSequence) > 0) return;
    if (packet[5] == PACKET_CLOSE) {
        adhoc_status(PSP_ADHOC_ERROR, "The host closed the room");
    } else if (packet[5] == PACKET_REJECT) {
        adhoc_status(PSP_ADHOC_ERROR, "Room full or previous connection is closing");
    } else if (packet[5] == PACKET_ACCEPT && (s32) (sequence - sAcceptedSequence) > 0) {
        sAcceptedSequence = sequence;
        sReceived = now;
        sView.roomId = packet[7];
        if (sequence == sSequence) sView.rttMs = (now - sSent) / 1000;
        adhoc_status(PSP_ADHOC_CONNECTED, "Connected - waiting for host");
    }
}

static void adhoc_prune(u32 now) {
    int i;
    sView.peerCount = 0;
    for (i = 0; i < 3; i++) {
        if (sPeers[i].token && now - sPeers[i].lastSeen >= ADHOC_PEER_TIMEOUT_US) sPeers[i].token = 0;
        if (sPeers[i].token) sView.peerCount++;
    }

}

static void adhoc_step(void) {
    static const u8 broadcast[6] = { 255, 255, 255, 255, 255, 255 };
    u32 now = sceKernelGetSystemTimeLow();
    unsigned events;
    int group = 0, result, i, error, lock;

    if (sceWlanGetSwitchState() <= 0) {
        adhoc_status(PSP_ADHOC_ERROR, "WLAN switched off - retry after enabling it");
        return;
    }
    lock = sceKernelCpuSuspendIntr();
    events = sControlEvents;
    error = sControlError;
    sControlEvents = 0;
    sceKernelCpuResumeIntr(lock);
    if (events & 1U) { adhoc_error("Radio event", error); return; }
    result = sceNetAdhocctlGetState(&group);
    if (result < 0) { adhoc_error("Group state", result); return; }
    if (sScanPending) {
        if (events & (1U << 3)) {
            sScanPending = 0;
            if (!sJoinPending && !adhoc_scan_results()) return;
        } else {
            if (now - sScanStarted >= ADHOC_CONNECT_US)
                adhoc_status(PSP_ADHOC_ERROR, "Radio scan timed out");
            return;
        }
    }
    if (sJoinPending) { adhoc_begin_join(); return; }
    if (!sHost && (sView.state == PSP_ADHOC_SEARCHING || sView.state == PSP_ADHOC_RESULTS)) {
        if (now - sStarted >= ADHOC_SEARCH_US) {
            sView.state = PSP_ADHOC_RESULTS;
        } else if (now - sLastScan >= 2000000U) {
            adhoc_clear_events();
            result = sceNetAdhocctlScan();
            if (result < 0) { adhoc_error("Radio scan", result); return; }
            sScanPending = 1;
            sScanStarted = sLastScan = now;
        }
        snprintf(sView.status, sizeof(sView.status), "Scans %d: %d radio / %d rooms",
                 sView.scans, sView.scanGroups, sView.roomCount);
        adhoc_publish();
        return;
    }
    if (sResources.socket < 0) {
        struct SceNetAdhocctlParams params;
        if (group != 1) {
            if (now - sStarted >= ADHOC_CONNECT_US)
                adhoc_status(PSP_ADHOC_ERROR, "Radio connection timed out");
            return;
        }
        memset(&params, 0, sizeof(params));
        result = sceNetAdhocctlGetParameter(&params);
        if (result < 0) { adhoc_error("Room details", result); return; }
        if (!sHost && (memcmp(params.bssid, sTarget.bssid, 6) != 0 ||
                       memcmp(params.name, sTarget.group, 8) != 0)) {
            adhoc_status(PSP_ADHOC_ERROR, "Joined a different radio network");
            return;
        }
        sView.channel = params.channel;
        result = sceNetAdhocPdpCreate(sMac, ADHOC_PORT, sizeof(sReceive), 0);
        if (result < 0) { adhoc_error("Socket create", result); return; }
        sResources.socket = result;
        sStarted = now;
        sSent = now - ADHOC_INTERVAL_US;
        adhoc_status(sHost ? PSP_ADHOC_HOSTING : PSP_ADHOC_JOINING,
                     sHost ? "Waiting for a watcher..." : "Waiting for host advertisement...");
    } else if (group != 1) {
        adhoc_status(PSP_ADHOC_ERROR, "Ad-Hoc connection lost");
        return;
    }
    adhoc_prune(now);
    for (i = 0; i < 8; i++) {
        unsigned char mac[8];
        unsigned short port;
        int length = sizeof(sReceive);
        result = sceNetAdhocPdpRecv(sResources.socket, mac, &port, sReceive, &length, 0, 1);
        if ((u32) result == ADHOC_WOULD_BLOCK) break;
        if (result < 0) { adhoc_error("Receive", result); return; }
        adhoc_packet(mac, port, sReceive, length, now);
        if (sView.state == PSP_ADHOC_ERROR) return;
    }
    if (sHost && now - sSent >= ADHOC_INTERVAL_US) {
        sSent = now;
        if (!adhoc_send(broadcast, PACKET_ADVERT, 0, 0)) return;
    } else if ((sView.state == PSP_ADHOC_JOINING || sView.state == PSP_ADHOC_CONNECTED) &&
               adhoc_has_mac(sTarget.mac) &&
               now - sSent >= ADHOC_INTERVAL_US) {
        sSent = now;
        sSequence++;
        if (!adhoc_send(sTarget.mac, PACKET_JOIN, sToken, sSequence)) return;
    }
    if (sView.state == PSP_ADHOC_JOINING && now - sStarted >= ADHOC_JOIN_US)
        adhoc_status(PSP_ADHOC_ERROR, "The host did not respond - search again");
    if (sView.state == PSP_ADHOC_CONNECTED && now - sReceived >= ADHOC_PEER_TIMEOUT_US)
        adhoc_status(PSP_ADHOC_ERROR, "The host connection timed out");
    if (sHost && sView.state == PSP_ADHOC_HOSTING)
        snprintf(sView.status, sizeof(sView.status), "Watchers connected: %d / 3", sView.peerCount);
    adhoc_world_send();
    adhoc_publish();
}

static void adhoc_leave(void) {
    int i;
    if (sResources.socket < 0) return;
    if (sHost) {
        for (i = 0; i < 3; i++) {
            if (sPeers[i].token) adhoc_send(sPeers[i].mac, PACKET_CLOSE, sPeers[i].token, sPeers[i].sequence);
        }
    } else if (adhoc_has_mac(sTarget.mac) &&
               (sView.state == PSP_ADHOC_CONNECTED || sView.state == PSP_ADHOC_JOINING)) {
        adhoc_send(sTarget.mac, PACKET_LEAVE, sToken, sSequence);
    }
}

static int adhoc_apply_request(const AdhocRequest* request) {
    sSerial = request->serial;
    sWorldSendingBytes = 0;
    sWorldSendingSequence = sWorldAssemblySequence = sWorldAssemblyMask = 0;
    if (request->action == REQUEST_JOIN && sResources.control && !sHost &&
        request->room.matchType == sMatchType &&
        (sView.state == PSP_ADHOC_SEARCHING || sView.state == PSP_ADHOC_RESULTS)) {
        sTarget = request->room;
        memset(sTarget.mac, 0, sizeof(sTarget.mac));
        sJoinPending = 1;
        sSession = sTarget.session;
        sView.roomId = sTarget.roomId;
        sToken = adhoc_nonce();
        sSequence = sAcceptedSequence = sProgressReceived = 0;
        sStarted = sceKernelGetSystemTimeLow();
        sSent = sStarted - ADHOC_INTERVAL_US;
        adhoc_status(PSP_ADHOC_JOINING, sScanPending ? "Finishing radio scan..." : "Joining host network...");
    } else {
        adhoc_leave();
        if (!adhoc_cleanup()) {
            if (request->action == REQUEST_EXIT) return 1;
            return 0;
        }
        if (adhoc_cancelled()) return 0;
        memset(&sView, 0, sizeof(sView));
        memset(sPeers, 0, sizeof(sPeers));
        if (request->action == REQUEST_EXIT) return 1;
        if (request->action == REQUEST_STOP) adhoc_publish();
        else if (request->action == REQUEST_JOIN) adhoc_status(PSP_ADHOC_ERROR, "Please search again");
        else {
            sHost = request->action == REQUEST_HOST;
            sMatchType = request->matchType;
            if (!adhoc_open()) adhoc_cleanup();
        }
    }
    return 0;
}

static int adhoc_worker(SceSize args, void* argp) {
    unsigned processed = 0;
    (void) args;
    (void) argp;

    for (;;) {
        AdhocRequest request;
        int lock = sceKernelCpuSuspendIntr();
        request = sRequest;
        sceKernelCpuResumeIntr(lock);
        if (request.serial != processed) {
            processed = request.serial;
            if (adhoc_apply_request(&request)) return 0;
        }
        if (adhoc_cancelled()) continue;
        if (sView.state == PSP_ADHOC_IDLE || sView.state == PSP_ADHOC_ERROR) {
            sceKernelSleepThread();
        } else {
            adhoc_step();
            if (sView.state == PSP_ADHOC_ERROR) adhoc_cleanup();
            sceKernelDelayThread(20000);
        }
    }
}

static void adhoc_request(int action, int matchType, const PspAdhocRoom* room) {
    int result, lock;
    if (sThread < 0 && (action == REQUEST_STOP || action == REQUEST_EXIT)) return;
    if (sThread < 0) {
        sThread = sceKernelCreateThread("sf64_adhoc", adhoc_worker, 0x68, 0x4000,
                                        PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
        if (sThread >= 0) {
            result = sceKernelStartThread(sThread, 0, NULL);
            if (result < 0) { sceKernelDeleteThread(sThread); sThread = result; }
        }
        if (sThread < 0) {
            lock = sceKernelCpuSuspendIntr();
            memset(&sPublished, 0, sizeof(sPublished));
            sPublished.state = PSP_ADHOC_ERROR;
            snprintf(sPublished.status, sizeof(sPublished.status), "Network worker: %08lX", (unsigned long) (u32) sThread);
            sceKernelCpuResumeIntr(lock);
            return;
        }
    }
    lock = sceKernelCpuSuspendIntr();
    sWorldPendingBytes = sWorldPublishedBytes = 0;
    sWorldPendingSequence = sWorldPublishedSequence = 0;
    sRequest.serial++;
    sRequest.action = action;
    sRequest.matchType = matchType;
    if (action == REQUEST_HOST) {
        sProgress.phase = PSP_ADHOC_HOST_LOBBY;
        sProgress.stage = 0;
        sProgress.frame = 0;
        sProgress.sequence = 1;
    }
    if (room) sRequest.room = *room;
    memset(&sPublished, 0, sizeof(sPublished));
    sPublished.state = action == REQUEST_STOP || action == REQUEST_EXIT ? PSP_ADHOC_STOPPING : PSP_ADHOC_STARTING;
    snprintf(sPublished.status, sizeof(sPublished.status), "%s", action == REQUEST_STOP ? "Closing Ad-Hoc..." : "Starting Ad-Hoc...");
    sceKernelCpuResumeIntr(lock);
    sceKernelWakeupThread(sThread);
}

void PspAdhoc_Host(int matchType) {
    if (matchType >= 0 && matchType < 3) adhoc_request(REQUEST_HOST, matchType, NULL);
}

void PspAdhoc_Search(int matchType) {
    if (matchType >= 0 && matchType < 3) adhoc_request(REQUEST_SEARCH, matchType, NULL);
}

void PspAdhoc_Join(const PspAdhocRoom* room) {
    if (room && room->session && room->matchType < 3) adhoc_request(REQUEST_JOIN, room->matchType, room);
}

void PspAdhoc_Stop(void) {
    adhoc_request(REQUEST_STOP, 0, NULL);
}

void PspAdhoc_GetView(PspAdhocView* view) {
    int lock = sceKernelCpuSuspendIntr();
    *view = sPublished;
    sceKernelCpuResumeIntr(lock);
}

void PspAdhoc_SetHostProgress(PspAdhocHostPhase phase, int stage, u32 frame) {
    int lock;
    if (phase < PSP_ADHOC_HOST_LOBBY || phase > PSP_ADHOC_HOST_PAUSED || stage < 0 || stage > 2) return;
    lock = sceKernelCpuSuspendIntr();
    if (sProgress.phase != phase || sProgress.stage != stage || sProgress.frame != frame) {
        sProgress.phase = phase;
        sProgress.stage = stage;
        sProgress.frame = frame;
        sProgress.sequence++;
    }
    sceKernelCpuResumeIntr(lock);
}

int PspAdhoc_PublishWorld(const void* data, int bytes) {
    int lock;
    if (!data || bytes <= 0 || bytes > PSP_ADHOC_WORLD_MAX_BYTES) return 0;
    lock = sceKernelCpuSuspendIntr();
    if (sRequest.action != REQUEST_HOST || sPublished.state != PSP_ADHOC_HOSTING) {
        sceKernelCpuResumeIntr(lock);
        return 0;
    }
    memcpy(sWorldPending, data, bytes);
    sWorldPendingBytes = bytes;
    if (++sWorldPendingSequence == 0) sWorldPendingSequence++;
    sceKernelCpuResumeIntr(lock);
    return 1;
}

int PspAdhoc_ReadWorld(void* data, int capacity, u32* sequence) {
    int bytes = 0, lock;
    if (!data || !sequence || capacity <= 0) return 0;
    lock = sceKernelCpuSuspendIntr();
    if (sRequest.action == REQUEST_JOIN && sPublished.state == PSP_ADHOC_CONNECTED &&
        sWorldPublishedBytes && capacity >= sWorldPublishedBytes && *sequence != sWorldPublishedSequence) {
        bytes = sWorldPublishedBytes;
        memcpy(data, sWorldPublished, bytes);
        *sequence = sWorldPublishedSequence;
    }
    sceKernelCpuResumeIntr(lock);
    return bytes;
}

void PspAdhoc_Shutdown(void) {
    SceUInt timeout = 1000000;
    if (sThread < 0) return;
    adhoc_request(REQUEST_EXIT, 0, NULL);
    if (sceKernelWaitThreadEnd(sThread, &timeout) == 0) {
        sceKernelDeleteThread(sThread);
        sThread = -1;
    }
}
