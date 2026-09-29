#ifndef PSP_COMMAND_SOURCE_H
#define PSP_COMMAND_SOURCE_H

#include "PR/gbi.h"
#include "PR/ultratypes.h"

#ifndef PROFILE_HW_COUNTERS
#define PROFILE_HW_COUNTERS 0
#endif

typedef enum {
    PSP_COMMAND_SOURCE_UNATTRIBUTED,
    PSP_COMMAND_SOURCE_STARFIELD,
    PSP_COMMAND_SOURCE_BACKDROP,
    PSP_COMMAND_SOURCE_GROUND,
    PSP_COMMAND_SOURCE_PLAYER,
    PSP_COMMAND_SOURCE_PLAYER_REFLECTION,
    PSP_COMMAND_SOURCE_SCENERY,
    PSP_COMMAND_SOURCE_BOSS,
    PSP_COMMAND_SOURCE_SPRITE,
    PSP_COMMAND_SOURCE_ACTOR,
    PSP_COMMAND_SOURCE_ITEM,
    PSP_COMMAND_SOURCE_SHOTS,
    PSP_COMMAND_SOURCE_EFFECTS,
    PSP_COMMAND_SOURCE_EFFECTS_REFLECTION,
    PSP_COMMAND_SOURCE_PLAYER_DETAILS,
    PSP_COMMAND_SOURCE_HUD,
    PSP_COMMAND_SOURCE_COUNT
} PspCommandSource;

typedef struct {
    u32 commands;
    u32 vertexCommands;
    u32 triangleCommands;
    u32 displayListCalls;
    u32 frontendUs;
} PspCommandSourceStats;

#define PSP_COMMAND_SOURCE_MAGIC 0x53524300u
#define PSP_COMMAND_SOURCE_TAG(source) (PSP_COMMAND_SOURCE_MAGIC | ((u32) (source) & 0xffu))
#define PSP_COMMAND_SOURCE_TAG_MATCH(tag) (((tag) & 0xffffff00u) == PSP_COMMAND_SOURCE_MAGIC)
#define PSP_COMMAND_SOURCE_TAG_ID(tag) ((u32) ((tag) & 0xffu))

#define PSP_WATER_TILE_COUNT 6
#define PSP_WATER_TILE_MAGIC 0x57540000u
#define PSP_WATER_TILE_TAG(tile) (PSP_WATER_TILE_MAGIC | ((u32) (tile) & 0xffu))
#define PSP_WATER_TILE_TAG_MATCH(tag) (((tag) & 0xffffff00u) == PSP_WATER_TILE_MAGIC)
#define PSP_WATER_TILE_TAG_ID(tag) ((u32) ((tag) & 0xffu))

#define PSP_EFFECT_TYPE_MAGIC 0x45460000u
#define PSP_EFFECT_TYPE_END 0xffffu
#define PSP_EFFECT_TYPE_TAG(id) (PSP_EFFECT_TYPE_MAGIC | ((u32) (id) & 0xffffu))
#define PSP_EFFECT_TYPE_TAG_MATCH(tag) (((tag) & 0xffff0000u) == PSP_EFFECT_TYPE_MAGIC)
#define PSP_EFFECT_TYPE_TAG_ID(tag) ((u32) ((tag) & 0xffffu))

#if PROFILE_HW_COUNTERS
#define PSP_COMMAND_SOURCE_MARK(pkt_expr, source) do { \
        Gfx* pspCommandSourcePacket = (pkt_expr); \
        gDPNoOpTag(pspCommandSourcePacket, PSP_COMMAND_SOURCE_TAG(source)); \
    } while (0)
#define PSP_WATER_TILE_MARK(pkt_expr, tile) gDPNoOpTag((pkt_expr), PSP_WATER_TILE_TAG(tile))
#define PSP_EFFECT_TYPE_MARK(pkt_expr, id) gDPNoOpTag((pkt_expr), PSP_EFFECT_TYPE_TAG(id))
#else
#define PSP_COMMAND_SOURCE_MARK(pkt_expr, source) ((void) 0)
#define PSP_WATER_TILE_MARK(pkt_expr, tile) ((void) 0)
#define PSP_EFFECT_TYPE_MARK(pkt_expr, id) ((void) 0)
#endif

#endif
