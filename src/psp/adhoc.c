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
#define ADHOC_INTERVAL_US 500000U
#define ADHOC_PEER_TIMEOUT_US 3000000U
#define ADHOC_SEARCH_US 10000000U
#define ADHOC_CONNECT_US 12000000U
#define ADHOC_JOIN_US 5000000U
#define ADHOC_WOULD_BLOCK 0x80410709U

enum { REQUEST_STOP, REQUEST_HOST, REQUEST_SEARCH, REQUEST_JOIN, REQUEST_EXIT };
enum { PACKET_ADVERT = 1, PACKET_JOIN, PACKET_ACCEPT, PACKET_LEAVE, PACKET_CLOSE, PACKET_REJECT };

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
} AdhocResources;

static SceUID sThread = -1;
static AdhocRequest sRequest;
static PspAdhocView sPublished;
static PspAdhocView sView;
static AdhocResources sResources = { 0, 0, 0, 0, 0, -1 };
static AdhocPeer sPeers[3];
static u32 sRoomSeen[PSP_ADHOC_MAX_ROOMS];
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
    u8 packet[ADHOC_PACKET_BYTES] = { 'S', 'F', 'A', 'L', 1 };
    int result;

    packet[5] = type;
    packet[6] = sMatchType;
    packet[7] = sView.roomId;
    adhoc_write_u32(packet + 8, sSession);
    adhoc_write_u32(packet + 12, token);
    adhoc_write_u32(packet + 16, sequence);
    result = sceNetAdhocPdpSend(sResources.socket, (unsigned char*) mac, ADHOC_PORT,
                               packet, sizeof(packet), 0, 1);
    if (result < 0 && (u32) result != ADHOC_WOULD_BLOCK) {
        adhoc_error("Send", result);
        return 0;
    }
    return 1;
}

static int adhoc_cleanup(void) {
    int result;
    if (sResources.socket >= 0) {
        result = sceNetAdhocPdpDelete(sResources.socket, 0);
        if (result < 0) { adhoc_error("Socket close", result); return 0; }
        sResources.socket = -1;
    }
    if (sResources.control) {
        sceNetAdhocctlDisconnect();
        result = sceNetAdhocctlTerm();
        if (result < 0) { adhoc_error("Control close", result); return 0; }
        sResources.control = 0;
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
    result = sceWlanGetEtherAddr(sMac);
    if (result < 0) { adhoc_error("WLAN address", result); return 0; }
    result = sceNetAdhocctlConnect("SF64LOBB");
    if (result < 0) { adhoc_error("Group connect", result); return 0; }
    sStarted = sceKernelGetSystemTimeLow();
    adhoc_status(PSP_ADHOC_STARTING, "Connecting to Ad-Hoc group...");
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
    u32 session = adhoc_read_u32(packet + 8);
    int i;
    if (sHost) {
        if (packet[7] == sView.roomId && memcmp(sMac, mac, 6) > 0) {
            sView.roomId = (sView.roomId + 1) % 100;
            sSent = now - ADHOC_INTERVAL_US;
        }
        return;
    }
    if (sView.state != PSP_ADHOC_SEARCHING && sView.state != PSP_ADHOC_RESULTS) return;
    for (i = 0; i < sView.roomCount; i++) {
        if (adhoc_same_room(&sView.rooms[i], mac, session)) break;
    }
    if (i == sView.roomCount) {
        if (sView.state != PSP_ADHOC_SEARCHING || i == PSP_ADHOC_MAX_ROOMS) return;
        sView.roomCount++;
    }
    memcpy(sView.rooms[i].mac, mac, 6);
    sView.rooms[i].roomId = packet[7];
    sView.rooms[i].matchType = packet[6];
    sView.rooms[i].session = session;
    sRoomSeen[i] = now;
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
    adhoc_send(mac, PACKET_ACCEPT, token, sequence);
}

static void adhoc_packet(const u8* mac, unsigned short port, const u8* packet, int length, u32 now) {
    u32 session, token, sequence;
    if (length != ADHOC_PACKET_BYTES || port != ADHOC_PORT || (mac[0] & 1) ||
        !(mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) ||
        memcmp(mac, sMac, 6) == 0 || memcmp(packet, "SFAL", 4) != 0 || packet[4] != 1 ||
        packet[5] < PACKET_ADVERT || packet[5] > PACKET_REJECT || packet[6] != sMatchType ||
        packet[7] > 99 || adhoc_read_u32(packet + 20) != 0) return;
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
        !adhoc_same_room(&sTarget, mac, session) || token != sToken ||
        (s32) (sequence - sSequence) > 0) return;
    if (packet[5] == PACKET_CLOSE) {
        adhoc_status(PSP_ADHOC_ERROR, "The host closed the room");
    } else if (packet[5] == PACKET_REJECT) {
        adhoc_status(PSP_ADHOC_ERROR, "Room full or previous connection is closing");
    } else if (packet[5] == PACKET_ACCEPT && (s32) (sequence - sAcceptedSequence) > 0) {
        sAcceptedSequence = sequence;
        sReceived = now;
        sView.roomId = packet[7];
        if (sequence == sSequence) sView.rttMs = (now - sSent) / 1000;
        adhoc_status(PSP_ADHOC_CONNECTED, "Lobby connected - watching comes next");
    }
}

static void adhoc_prune(u32 now) {
    int i;
    sView.peerCount = 0;
    for (i = 0; i < 3; i++) {
        if (sPeers[i].token && now - sPeers[i].lastSeen >= ADHOC_PEER_TIMEOUT_US) sPeers[i].token = 0;
        if (sPeers[i].token) sView.peerCount++;
    }
    for (i = sView.roomCount - 1; i >= 0; i--) {
        if (now - sRoomSeen[i] >= ADHOC_PEER_TIMEOUT_US) {
            int count = sView.roomCount - i - 1;
            memmove(sView.rooms + i, sView.rooms + i + 1, count * sizeof(sView.rooms[0]));
            memmove(sRoomSeen + i, sRoomSeen + i + 1, count * sizeof(sRoomSeen[0]));
            sView.roomCount--;
        }
    }
}

static void adhoc_step(void) {
    static const u8 broadcast[6] = { 255, 255, 255, 255, 255, 255 };
    u32 now = sceKernelGetSystemTimeLow();
    int group = 0, result, i;

    if (sceWlanGetSwitchState() <= 0) {
        adhoc_status(PSP_ADHOC_ERROR, "WLAN switched off - retry after enabling it");
        return;
    }
    result = sceNetAdhocctlGetState(&group);
    if (result < 0) { adhoc_error("Group state", result); return; }
    if (sView.state == PSP_ADHOC_STARTING) {
        if (group != 1) {
            if (now - sStarted >= ADHOC_CONNECT_US)
                adhoc_status(PSP_ADHOC_ERROR, "Ad-Hoc timeout - check both WLAN channels");
            return;
        }
        result = sceNetAdhocPdpCreate(sMac, ADHOC_PORT, sizeof(sReceive), 0);
        if (result < 0) { adhoc_error("Socket create", result); return; }
        sResources.socket = result;
        sSession = adhoc_nonce();
        sView.roomId = sSession % 100;
        sStarted = now;
        sSent = now - ADHOC_INTERVAL_US;
        adhoc_status(sHost ? PSP_ADHOC_HOSTING : PSP_ADHOC_SEARCHING,
                     sHost ? "Waiting for a watcher..." : "Searching...");
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
               now - sSent >= ADHOC_INTERVAL_US) {
        sSent = now;
        sSequence++;
        if (!adhoc_send(sTarget.mac, PACKET_JOIN, sToken, sSequence)) return;
    }
    if (sView.state == PSP_ADHOC_SEARCHING && now - sStarted >= ADHOC_SEARCH_US)
        adhoc_status(PSP_ADHOC_RESULTS, sView.roomCount ? "Select a room or search again" : "No games found");
    if (sView.state == PSP_ADHOC_JOINING && now - sStarted >= ADHOC_JOIN_US)
        adhoc_status(PSP_ADHOC_ERROR, "The host did not respond - search again");
    if (sView.state == PSP_ADHOC_CONNECTED && now - sReceived >= ADHOC_PEER_TIMEOUT_US)
        adhoc_status(PSP_ADHOC_ERROR, "The host connection timed out");
    if (sHost && sView.state == PSP_ADHOC_HOSTING)
        snprintf(sView.status, sizeof(sView.status), "Watchers connected: %d / 3", sView.peerCount);
    adhoc_publish();
}

static void adhoc_leave(void) {
    int i;
    if (sResources.socket < 0) return;
    if (sHost) {
        for (i = 0; i < 3; i++) {
            if (sPeers[i].token) adhoc_send(sPeers[i].mac, PACKET_CLOSE, sPeers[i].token, sPeers[i].sequence);
        }
    } else if (sView.state == PSP_ADHOC_CONNECTED || sView.state == PSP_ADHOC_JOINING) {
        adhoc_send(sTarget.mac, PACKET_LEAVE, sToken, sSequence);
    }
}

static int adhoc_apply_request(const AdhocRequest* request) {
    sSerial = request->serial;
    if (request->action == REQUEST_JOIN && sResources.socket >= 0 && !sHost &&
        request->room.matchType == sMatchType &&
        (sView.state == PSP_ADHOC_SEARCHING || sView.state == PSP_ADHOC_RESULTS)) {
        sTarget = request->room;
        sSession = sTarget.session;
        sView.roomId = sTarget.roomId;
        sToken = adhoc_nonce();
        sSequence = sAcceptedSequence = 0;
        sStarted = sceKernelGetSystemTimeLow();
        sSent = sStarted - ADHOC_INTERVAL_US;
        adhoc_status(PSP_ADHOC_JOINING, "Connecting to host...");
    } else {
        adhoc_leave();
        if (!adhoc_cleanup()) {
            if (request->action == REQUEST_EXIT) return 1;
            return 0;
        }
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
        sThread = sceKernelCreateThread("sf64_adhoc", adhoc_worker, 0x68, 0x4000, 0, NULL);
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
    sRequest.serial++;
    sRequest.action = action;
    sRequest.matchType = matchType;
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

void PspAdhoc_Shutdown(void) {
    SceUInt timeout = 1000000;
    if (sThread < 0) return;
    adhoc_request(REQUEST_EXIT, 0, NULL);
    if (sceKernelWaitThreadEnd(sThread, &timeout) == 0) {
        sceKernelDeleteThread(sThread);
        sThread = -1;
    }
}
