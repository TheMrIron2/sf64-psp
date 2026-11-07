#include "global.h"
#include "src/psp/adhoc.h"
#include "src/psp/platform.h"
#include "src/psp/input.h"
#include "src/psp/spectate.h"

extern s32 sVsDrawMode, sStartCountdown, sVsWinner, D_80178850[4];
void Versus_InitMatch(void);

enum { WORLD_ACTOR, WORLD_ITEM, WORLD_EFFECT, WORLD_SCENERY };

typedef struct {
    u32 magic, schema, bytes, epoch;
    s32 frame, allRangeFrame, stage, matchType, points, timeLimit, paused;
    s32 matchStart, matchOver, matchWon, matchState, drawMode, countdown, winner;
    s32 inactive[4], cameraLabels[4], bombs[4], pointsScored[4], kills[4][10], countdownTime[3];
    s32 laser[4], reticles[4];
    u16 scores[4];
    u32 glareAlpha[4];
    s32 glareRed[4], glareGreen[4], glareBlue[4];
    f32 fov, viewScale;
    u16 background;
    u8 fillRed, fillGreen, fillBlue, fillAlpha;
    Player player[4];
    PlayerShot shot[16];
} SpectateWorld;

typedef char SpectateHeaderFits[(sizeof(SpectateWorld) < PSP_ADHOC_WORLD_MAX_BYTES) ? 1 : -1];

typedef struct { u16 bytes; u8 type, index; } SpectateRecord;

static union { u32 aligned; u8 data[PSP_ADHOC_WORLD_MAX_BYTES]; } sWorld, sIncoming;
static union { Player player; Actor actor; Item item; Effect effect; Object object; } sDecode;
static int sBytes, sReady, sInvalid;
static volatile int sWatching, sStop;
static u32 sReceivedSequence, sEpoch, sHostEpoch, sLastCapture, sLastUpdate;
static int sHostNeedsEpoch = 1;
static const char* volatile sStatus = "Waiting for a world snapshot";
static const char* volatile sInvalidReason;
static int sInvalidIndex;
static u32 sInvalidValue;
static const char* volatile sLoggedReason;
static int sLoggedIndex = -2;

static int spectate_reject(const char* reason, int index) {
    sInvalidReason = reason;
    sInvalidIndex = index;
    return 0;
}

static void spectate_zero(void* data, unsigned bytes) {
    u8* out = data;
    unsigned i;
    for (i = 0; i < bytes; i++) out[i] = 0;
}

static u32 spectate_schema(void) {
    return 0x53570100U ^ sizeof(SpectateWorld) ^ (sizeof(Player) << 12) ^
           (sizeof(Actor) << 4) ^ (sizeof(Effect) << 20) ^ (sizeof(Item) << 8);
}

static int spectate_vector(const Vec3f* v) {
    union { f32 f; u32 u; } bits;
    if (!__builtin_isfinite(v->x) || fabsf(v->x) >= 10000000.0f) { bits.f = v->x; sInvalidValue = bits.u; return 0; }
    if (!__builtin_isfinite(v->y) || fabsf(v->y) >= 10000000.0f) { bits.f = v->y; sInvalidValue = bits.u; return 0; }
    if (!__builtin_isfinite(v->z) || fabsf(v->z) >= 10000000.0f) { bits.f = v->z; sInvalidValue = bits.u; return 0; }
    return 1;
}

static int spectate_object(const Object* obj, int type) {
    if (!obj->status || obj->status > OBJ_DYING || !spectate_vector(&obj->pos) ||
        !spectate_vector(&obj->rot)) return 0;
    if (type == WORLD_ACTOR) return obj->id >= OBJ_ACTOR_START && obj->id < OBJ_ACTOR_MAX;
    if (type == WORLD_ITEM) return obj->id >= OBJ_ITEM_START && obj->id < OBJ_ITEM_MAX;
    if (type == WORLD_EFFECT) return obj->id >= OBJ_EFFECT_START && obj->id < OBJ_EFFECT_MAX;
    return obj->id < OBJ_SCENERY_MAX;
}

static int spectate_size(int type) {
    switch (type) {
        case WORLD_ACTOR: return sizeof(Actor);
        case WORLD_ITEM: return sizeof(Item);
        case WORLD_EFFECT: return sizeof(Effect);
        case WORLD_SCENERY: return sizeof(Object);
    }
    return 0;
}

static int spectate_records(const u8* data, int bytes, int commit) {
    int at = sizeof(SpectateWorld), i;
    u8 seen[4][200];
    spectate_zero(seen, sizeof(seen));
    if (commit) {
        for (i = 0; i < 60; i++) gActors[i].obj.status = OBJ_FREE;
        for (i = 0; i < 20; i++) gItems[i].obj.status = OBJ_FREE;
        for (i = 0; i < 100; i++) gEffects[i].obj.status = OBJ_FREE;
        for (i = 0; i < 200; i++) gScenery360[i].obj.status = OBJ_FREE;
    }
    while (at < bytes) {
        SpectateRecord record;
        ObjectInfo* info = NULL;
        Object* target = NULL;
        int limit;
        if (bytes - at < (int) sizeof(record)) return spectate_reject("record truncated", at);
        memcpy(&record, data + at, sizeof(record));
        at += sizeof(record);
        if (record.type > WORLD_SCENERY || record.bytes != spectate_size(record.type) ||
            record.bytes > bytes - at) return spectate_reject("record header", record.type);
        limit = record.type == WORLD_ACTOR ? 60 : record.type == WORLD_ITEM ? 20 :
                record.type == WORLD_EFFECT ? 100 : 200;
        if (record.index >= limit || seen[record.type][record.index]++)
            return spectate_reject("record index", record.index);
        memcpy(&sDecode, data + at, record.bytes);
        if (!spectate_object(&sDecode.object, record.type))
            return spectate_reject("record object", record.index);
        if (record.type != WORLD_SCENERY) {
            const u8* raw = (const u8*) &sDecode;
            for (i = sizeof(Object); i < (int) (sizeof(Object) + sizeof(ObjectInfo)); i++)
                if (raw[i]) return spectate_reject("record info", record.index);
        }
        if (record.type == WORLD_EFFECT && sDecode.effect.dList)
            return spectate_reject("effect pointer", record.index);
        if (commit) {
            switch (record.type) {
                case WORLD_ACTOR:
                    target = &gActors[record.index].obj; info = &gActors[record.index].info; break;
                case WORLD_ITEM:
                    target = &gItems[record.index].obj; info = &gItems[record.index].info; break;
                case WORLD_EFFECT:
                    target = &gEffects[record.index].obj; info = &gEffects[record.index].info; break;
                case WORLD_SCENERY:
                    target = &gScenery360[record.index].obj; info = &gScenery360[record.index].info; break;
            }
            memcpy(target, &sDecode, record.bytes);
            Object_SetInfo(info, target->id);
        }
        at += record.bytes;
    }
    return at == bytes ? 1 : spectate_reject("record length", at);
}

static int spectate_valid(const u8* data, int bytes) {
    const SpectateWorld* world = (const SpectateWorld*) data;
    int i, j;
    if (bytes < (int) sizeof(*world)) return spectate_reject("short world", bytes);
    if (world->magic != 0x53505731U) return spectate_reject("bad magic", -1);
    if (world->schema != spectate_schema()) return spectate_reject("bad schema", -1);
    if (world->bytes != (u32) bytes) return spectate_reject("bad size", world->bytes);
    if (!world->epoch) return spectate_reject("bad epoch", -1);
    if (world->stage < 0 || world->stage > 2) return spectate_reject("bad stage", world->stage);
    if (world->matchType < 0 || world->matchType > 2) return spectate_reject("bad match type", world->matchType);
    if (world->matchStart < 0 || world->matchStart > 1) return spectate_reject("bad match start", world->matchStart);
    if (world->matchOver < 0 || world->matchOver > 1) return spectate_reject("bad match over", world->matchOver);
    if (world->matchType == VS_MATCH_TIME ? world->points != 10000 :
        (world->points < 1 || world->points > 10)) return spectate_reject("bad points", world->points);
    if (world->paused < 0 || world->paused > 1) return spectate_reject("bad paused", world->paused);
    if (world->matchWon < 0 || world->matchWon > 1) return spectate_reject("bad match won", world->matchWon);
    if (world->matchState < 0 || world->matchState > VS_STATE_20)
        return spectate_reject("bad match state", world->matchState);
    if (!__builtin_isfinite(world->fov) || world->fov < 10 || world->fov > 100)
        return spectate_reject("bad fov", -1);
    if (!__builtin_isfinite(world->viewScale) || world->viewScale < 0.5f || world->viewScale > 1.5f)
        return spectate_reject("bad view scale", -1);
    if (world->drawMode < 0 || world->drawMode > 11) return spectate_reject("bad draw mode", world->drawMode);
    if (world->countdown < 0 || world->countdown > 4) return spectate_reject("bad countdown", world->countdown);
    if (world->drawMode == 2 && world->countdown > 3)
        return spectate_reject("bad countdown draw", world->countdown);
    if (world->winner != 99 && (world->winner < 0 || world->winner > 3))
        return spectate_reject("bad winner", world->winner);
    for (i = 0; i < 4; i++) {
        const Player* player = &world->player[i];
        if (player->sfx.srcPos || player->sfx.srcVel) return spectate_reject("player sfx", i);
        if (player->num != i) return spectate_reject("player num", i);
        if (player->state < 0 || player->state >= PLAYERSTATE_MAX) return spectate_reject("player state", i);
        if (player->form < FORM_ARWING || player->form > FORM_ON_FOOT) return spectate_reject("player form", i);
        if (!spectate_vector(&player->pos)) return spectate_reject("player pos", i);
        if (!spectate_vector(&player->cam.eye)) return spectate_reject("player cam eye", i);
        if (!spectate_vector(&player->cam.at)) return spectate_reject("player cam at", i);
        if (!spectate_vector(&player->rot)) return spectate_reject("player rot", i);
        if (world->inactive[i] < 0 || world->inactive[i] > 1) return spectate_reject("player inactive", i);
        if (world->cameraLabels[i] < 0 || world->cameraLabels[i] > 1)
            return spectate_reject("player cam label", i);
        if (world->pointsScored[i] < 0 || world->pointsScored[i] > 10)
            return spectate_reject("player points", i);
        if (world->laser[i] < 0 || world->laser[i] >= LASERS_MAX) return spectate_reject("player laser", i);
        if (world->reticles[i] < 0 || world->reticles[i] > 1) return spectate_reject("player reticle", i);
        for (j = 0; j < 10; j++) {
            if (world->kills[i][j] < 0 || world->kills[i][j] > 3) return spectate_reject("player kills", i);
        }
    }
    for (i = 0; i < 16; i++) {
        if (world->shot[i].obj.status > SHOT_HITMARK) return spectate_reject("shot status", i);
        if (world->shot[i].obj.id > PLAYERSHOT_GFOX_LASER) return spectate_reject("shot id", i);
        if (!spectate_vector(&world->shot[i].obj.pos)) return spectate_reject("shot pos", i);
        if (!spectate_vector(&world->shot[i].obj.rot)) return spectate_reject("shot rot", i);
    }
    return spectate_records(data, bytes, 0);
}

#define WORLD_ARRAY(field, global) memcpy(world->field, global, sizeof(world->field))
#define APPLY_ARRAY(global, field) memcpy(global, world->field, sizeof(world->field))

static void spectate_apply(void) {
    SpectateWorld* world = (SpectateWorld*) sWorld.data;
    int i;
    memcpy(gPlayer, world->player, sizeof(world->player));
    memcpy(gPlayerShots, world->shot, sizeof(world->shot));
    for (i = 0; i < 4; i++) {
        gPlayer[i].sfx.srcPos = gPlayer[i].sfxSource;
        gPlayer[i].sfx.srcVel = gPlayer[i].sfxVel;
    }
    spectate_records(sWorld.data, sBytes, 1);
    APPLY_ARRAY(gPlayerInactive, inactive); APPLY_ARRAY(D_80178850, cameraLabels);
    APPLY_ARRAY(gBombCount, bombs); APPLY_ARRAY(gVsPoints, pointsScored); APPLY_ARRAY(gVsKills, kills);
    APPLY_ARRAY(gVsCountdown, countdownTime); APPLY_ARRAY(gLaserStrength, laser);
    APPLY_ARRAY(gShowReticles, reticles); APPLY_ARRAY(gPlayerScores, scores);
    APPLY_ARRAY(gPlayerGlareAlphas, glareAlpha); APPLY_ARRAY(gPlayerGlareReds, glareRed);
    APPLY_ARRAY(gPlayerGlareGreens, glareGreen); APPLY_ARRAY(gPlayerGlareBlues, glareBlue);
    gGameFrameCount = world->frame;
    gAllRangeFrameCount = world->allRangeFrame;
    gFovY = world->fov; gNextVsViewScale = gVsViewScale = world->viewScale;
    gBgColor = world->background;
    gVsMatchStart = world->matchStart; gVsMatchOver = world->matchOver;
    gVsMatchWon = world->matchWon; gVsMatchState = world->matchState;
    sVsDrawMode = world->drawMode; sStartCountdown = world->countdown; sVsWinner = world->winner;
    gFillScreenRed = world->fillRed; gFillScreenGreen = world->fillGreen;
    gFillScreenBlue = world->fillBlue; gFillScreenAlpha = world->fillAlpha;
    gPlayState = world->paused ? PLAY_PAUSE : PLAY_UPDATE;
    gCamCount = 4;
    gVersusMode = true;
    gDrawMode = DRAW_PLAY;
}

void PspSpectate_Start(void) {
    if (sWatching) return;
    sWatching = 1; sStop = sReady = sInvalid = sBytes = 0;
    sReceivedSequence = sEpoch = 0;
    sInvalidReason = NULL; sInvalidIndex = -1; sInvalidValue = 0;
    sLoggedReason = NULL; sLoggedIndex = -2;
    sLastUpdate = PspPlatform_GetViCount();
    sStatus = "Loading the host world";
}

void PspSpectate_Stop(void) { sStop = 1; }
int PspSpectate_IsWatching(void) { return sWatching; }
const char* PspSpectate_Status(void) { return sStatus; }

void PspSpectate_BeforeGame(void) {
    PspAdhocView view;
    SpectateWorld* world = (SpectateWorld*) sWorld.data;
    int bytes;
    if (!sWatching) {
        if (gGameState != GSTATE_PLAY || gCurrentLevel != LEVEL_VERSUS || gPlayState == PLAY_INIT)
            sHostNeedsEpoch = 1;
        return;
    }
    PspAdhoc_GetView(&view);
    if (sStop || view.state != PSP_ADHOC_CONNECTED) {
        PspInput_RequireRelease();
        sWatching = sStop = sReady = 0;
        sStatus = "Spectating ended";
        PspAdhoc_Stop();
        gCamCount = 1;
        gVersusMode = false;
        gVsMatchStart = false;
        gPlayerGlareAlphas[0] = 0;
        gNextGameState = GSTATE_MENU;
        gOptionMenuStatus = OPTION_WAIT;
        return;
    }
    bytes = PspAdhoc_ReadWorld(sIncoming.data, sizeof(sIncoming.data), &sReceivedSequence);
    if (bytes) {
        const SpectateWorld* incoming = (const SpectateWorld*) sIncoming.data;
        sInvalidReason = NULL;
        sInvalidIndex = -1;
        sInvalidValue = 0;
        sInvalid = !spectate_valid(sIncoming.data, bytes);
        if (!sInvalid && sEpoch && incoming->epoch != sEpoch && (s32) (incoming->epoch - sEpoch) <= 0) {
            sInvalid = 1;
            sInvalidReason = "stale epoch";
        } else if (!sInvalid && sEpoch && incoming->epoch == sEpoch &&
                   (incoming->stage != gVersusStage || incoming->matchType != gVsMatchType)) {
            sInvalid = 1;
            sInvalidReason = "stage changed";
        }
        if (sInvalid) {
            if (sInvalidReason != sLoggedReason || sInvalidIndex != sLoggedIndex) {
                sLoggedReason = sInvalidReason;
                sLoggedIndex = sInvalidIndex;
                PspPlatform_LogValue(sInvalidReason ? sInvalidReason : "invalid snapshot",
                                     (u32) (sInvalidIndex + 1));
                PspPlatform_LogValue("invalid value bits", sInvalidValue);
            }
            sStatus = sInvalidReason ? sInvalidReason : "Dropped incompatible or invalid snapshot";
            return;
        }
        memcpy(sWorld.data, sIncoming.data, bytes);
        sBytes = bytes;
        sLastUpdate = PspPlatform_GetViCount();
        if (world->epoch != sEpoch) {
            sEpoch = world->epoch;
            sReady = 0;
            gCamCount = 1;
            gVersusMode = false;
            gPlayerGlareAlphas[0] = 0;
            gVersusStage = world->stage; gVsMatchType = world->matchType;
            gVsPointsToWin = world->points; gVsTimeTrialLimit = world->timeLimit;
            gNextGameState = GSTATE_VS_INIT;
            gOptionMenuStatus = OPTION_UPDATE;
            sStatus = "Loading the host arena";
            return;
        }
    }
    if (!sBytes) return;
    if (gGameState == GSTATE_PLAY && gCurrentLevel == LEVEL_VERSUS &&
        (gPlayState == PLAY_UPDATE || gPlayState == PLAY_PAUSE) && gPlayer && gScenery360) {
        if (!sReady) {
            APPLY_ARRAY(gPlayerInactive, inactive);
            Versus_InitMatch();
        }
        sReady = 1;
        spectate_apply();
        sStatus = sInvalid ? (sInvalidReason ? sInvalidReason : "Dropped incompatible or invalid snapshot") :
                  PspPlatform_GetViCount() - sLastUpdate > 60 ? "Waiting for world updates" :
                  view.hostPhase == PSP_ADHOC_HOST_SETUP ? "Host is choosing match settings" :
                  world->paused ? "Watching - host paused" :
                  world->matchOver ? "Watching - match ended" : "Watching the host game";
    }
}

int PspSpectate_OverrideSimulation(void) {
    if (!sWatching || gGameState != GSTATE_PLAY ||
        (gPlayState != PLAY_UPDATE && gPlayState != PLAY_PAUSE)) return 0;
    if (!sReady) gDrawMode = DRAW_NONE;
    return 1;
}

static int spectate_append(int at, int type, int index, const void* source) {
    SpectateRecord record;
    int bytes = spectate_size(type);
    if (at + (int) sizeof(record) + bytes > PSP_ADHOC_WORLD_MAX_BYTES) return -1;
    record.bytes = bytes; record.type = type; record.index = index;
    memcpy(sWorld.data + at, &record, sizeof(record));
    at += sizeof(record);
    memcpy(sWorld.data + at, source, bytes);
    if (type != WORLD_SCENERY) spectate_zero(sWorld.data + at + sizeof(Object), sizeof(ObjectInfo));
    return at + bytes;
}

void PspSpectate_Capture(void) {
    PspAdhocView view;
    SpectateWorld* world = (SpectateWorld*) sWorld.data;
    u32 vi = PspPlatform_GetViCount();
    int i, at = sizeof(*world);
    if (sWatching || gGameState != GSTATE_PLAY || gCurrentLevel != LEVEL_VERSUS ||
        !gPlayer || !gScenery360 || (gPlayState != PLAY_UPDATE && gPlayState != PLAY_PAUSE) ||
        vi - sLastCapture < 4 || gVsViewScale < 0.5f) return;
    PspAdhoc_GetView(&view);
    if (view.state != PSP_ADHOC_HOSTING || !view.peerCount) return;
    for (i = 0; i < 4; i++) if (gPlayer[i].num != i) return;
    sLastCapture = vi;
    if (!sHostEpoch || sHostNeedsEpoch) {
        if (++sHostEpoch == 0) sHostEpoch++;
        sHostNeedsEpoch = 0;
    }
    spectate_zero(world, sizeof(*world));
    world->magic = 0x53505731U; world->schema = spectate_schema(); world->epoch = sHostEpoch;
    world->frame = gGameFrameCount; world->allRangeFrame = gAllRangeFrameCount; world->stage = gVersusStage; world->matchType = gVsMatchType;
    world->points = gVsPointsToWin; world->timeLimit = gVsTimeTrialLimit;
    world->paused = gPlayState == PLAY_PAUSE;
    // gVsMatchStart is an int bool and is incremented to 2 once the match begins
    world->matchStart = gVsMatchStart != 0; world->matchOver = gVsMatchOver != 0;
    world->matchWon = gVsMatchWon != 0; world->matchState = gVsMatchState;
    world->drawMode = sVsDrawMode; world->countdown = sStartCountdown; world->winner = sVsWinner;
    world->fov = gFovY; world->viewScale = gVsViewScale; world->background = gBgColor;
    world->fillRed = gFillScreenRed; world->fillGreen = gFillScreenGreen;
    world->fillBlue = gFillScreenBlue; world->fillAlpha = gFillScreenAlpha;
    WORLD_ARRAY(inactive, gPlayerInactive); WORLD_ARRAY(cameraLabels, D_80178850);
    WORLD_ARRAY(bombs, gBombCount); WORLD_ARRAY(pointsScored, gVsPoints); WORLD_ARRAY(kills, gVsKills);
    WORLD_ARRAY(countdownTime, gVsCountdown); WORLD_ARRAY(laser, gLaserStrength);
    WORLD_ARRAY(reticles, gShowReticles); WORLD_ARRAY(scores, gPlayerScores);
    WORLD_ARRAY(glareAlpha, gPlayerGlareAlphas); WORLD_ARRAY(glareRed, gPlayerGlareReds);
    WORLD_ARRAY(glareGreen, gPlayerGlareGreens); WORLD_ARRAY(glareBlue, gPlayerGlareBlues);
    memcpy(world->player, gPlayer, sizeof(world->player));
    memcpy(world->shot, gPlayerShots, sizeof(world->shot));
    for (i = 0; i < 4; i++) world->player[i].sfx.srcPos = world->player[i].sfx.srcVel = NULL;
    for (i = 0; i < 60 && at >= 0; i++) if (gActors[i].obj.status)
        at = spectate_append(at, WORLD_ACTOR, i, &gActors[i]);
    for (i = 0; i < 20 && at >= 0; i++) if (gItems[i].obj.status)
        at = spectate_append(at, WORLD_ITEM, i, &gItems[i]);
    for (i = 0; i < 100 && at >= 0; i++) if (gEffects[i].obj.status && !gEffects[i].dList)
        at = spectate_append(at, WORLD_EFFECT, i, &gEffects[i]);
    for (i = 0; i < 200 && at >= 0; i++) if (gScenery360[i].obj.status)
        at = spectate_append(at, WORLD_SCENERY, i, &gScenery360[i].obj);
    if (at < 0) return;
    world->bytes = at;
    PspAdhoc_PublishWorld(sWorld.data, at);
}
