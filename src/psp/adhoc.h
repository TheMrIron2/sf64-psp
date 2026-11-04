#ifndef PSP_ADHOC_H
#define PSP_ADHOC_H

#include "PR/ultratypes.h"

#define PSP_ADHOC_MAX_ROOMS 8

typedef enum {
    PSP_ADHOC_IDLE,
    PSP_ADHOC_STARTING,
    PSP_ADHOC_HOSTING,
    PSP_ADHOC_SEARCHING,
    PSP_ADHOC_RESULTS,
    PSP_ADHOC_JOINING,
    PSP_ADHOC_CONNECTED,
    PSP_ADHOC_STOPPING,
    PSP_ADHOC_ERROR
} PspAdhocState;

typedef struct {
    u8 mac[6];
    u8 roomId;
    u8 matchType;
    u32 session;
} PspAdhocRoom;

typedef struct {
    PspAdhocState state;
    int roomCount;
    PspAdhocRoom rooms[PSP_ADHOC_MAX_ROOMS];
    int roomId;
    int peerCount;
    int rttMs;
    char status[60];
} PspAdhocView;

void PspAdhoc_Host(int matchType);
void PspAdhoc_Search(int matchType);
void PspAdhoc_Join(const PspAdhocRoom* room);
void PspAdhoc_Stop(void);
void PspAdhoc_GetView(PspAdhocView* view);
void PspAdhoc_Shutdown(void);

#endif
