#include "global.h"
#include "fox_map.h"

extern PlanetId sPlanetList[15];
extern PlanetId sCurrentPlanetId;
extern bool sLevelStartState;
extern s32 D_menu_801CD968;
extern s32 sMapState;
extern s32 sMapSubState;

void Map_LevelStart_AudioSpecSetup(LevelId level);
void Map_CurrentLevel_Setup(void);
void Map_PositionCursor(void);
void Map_PlayLevel(void);

static PlanetId sPlanetArray[][3] = {
    { PLANET_CORNERIA, PLANET_CORNERIA, PLANET_CORNERIA }, { PLANET_METEO, PLANET_METEO, PLANET_SECTOR_Y },
    { PLANET_FORTUNA, PLANET_KATINA, PLANET_AQUAS },       { PLANET_SECTOR_X, PLANET_SOLAR, PLANET_ZONESS },
    { PLANET_TITANIA, PLANET_MACBETH, PLANET_SECTOR_Z },   { PLANET_BOLSE, PLANET_BOLSE, PLANET_AREA_6 },
    { PLANET_VENOM, PLANET_VENOM, SAVE_SLOT_VENOM_2 },
};

static s32 sLevelSelectMission;
static s32 sLevelSelectDifficulty;
static s32 sLevelSelectStartOption;

void Map_LevelSelect(void) {
    enum {
        LEVELSELECT_START_NORMAL,
        LEVELSELECT_START_TUNNEL,
        LEVELSELECT_START_ANDROSS,
    };
    // static f32 zStart = 0.0f;
    // f32 zInc;
    s32 nextPlanetId;
    OSContPad* contPress = &gControllerPress[gMainController];

    if (contPress->button & L_JPAD) {
        sLevelSelectMission--;
        if (sLevelSelectMission < 0) {
            sLevelSelectMission = 6;
        }
    } else if (contPress->button & R_JPAD) {
        sLevelSelectMission++;
        if (sLevelSelectMission > 6) {
            sLevelSelectMission = 0;
        }
    } else if ((contPress->button & U_JPAD) && (sLevelSelectMission != 0)) {
        sLevelSelectDifficulty++;
        if (sLevelSelectDifficulty > 2) {
            sLevelSelectDifficulty = 0;
        }
        if ((sLevelSelectDifficulty == 1) &&
            ((sLevelSelectMission == 1) || (sLevelSelectMission == 5) || (sLevelSelectMission == 6))) {
            sLevelSelectDifficulty = 2;
        }
    } else if ((contPress->button & D_JPAD) && (sLevelSelectMission != 0)) {
        sLevelSelectDifficulty--;
        if ((sLevelSelectDifficulty != 2) &&
            ((sLevelSelectMission == 1) || (sLevelSelectMission == 5) || (sLevelSelectMission == 6))) {
            sLevelSelectDifficulty--;
        }
        if (sLevelSelectDifficulty < 0) {
            sLevelSelectDifficulty = 2;
        }
    }

    nextPlanetId = (sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty] == SAVE_SLOT_VENOM_2)
                       ? PLANET_VENOM
                       : sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty];
    if (sCurrentPlanetId != nextPlanetId) {
        sCurrentPlanetId = nextPlanetId;
        sLevelSelectStartOption = 0;
        Map_CurrentLevel_Setup();
        Map_PositionCursor();
    }
    if ((sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty] != SAVE_SLOT_VENOM_2) &&
        (sLevelSelectStartOption > LEVELSELECT_START_TUNNEL)) {
        sLevelSelectStartOption = LEVELSELECT_START_NORMAL;
    }
    if (contPress->button & (L_TRIG | Z_TRIG)) {
        if (sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty] == SAVE_SLOT_VENOM_2) {
            sLevelSelectStartOption++;
            if (sLevelSelectStartOption > LEVELSELECT_START_ANDROSS) {
                sLevelSelectStartOption = LEVELSELECT_START_NORMAL;
            }
        } else {
            sLevelSelectStartOption ^= 1;
        }
    }

    // if (contPress->button & U_CBUTTONS) {
    //     zInc = 100.0f;
    // } else if (contPress->button & R_CBUTTONS) {
    //     zInc = 1000.0f;
    // } else if (contPress->button & D_CBUTTONS) {
    //     zInc = -100.0f;
    // } else if (contPress->button & L_CBUTTONS) {
    //     zInc = -1000.0f;
    // }
    // if (gControllerHold[0].button & R_TRIG) {
    //     zInc *= 100.0f;
    // }
    // zStart += zInc;
    // if(zStart < 0.0f) {
    //     zStart = 0.0f;
    // } else if (zStart > 500000.0f) {
    //     zStart = 500000.0f;
    // }

    // Bypass briefing
    if ((sMapState == 2) && (sMapSubState > 0)) {
        if (sCurrentPlanetId == PLANET_VENOM) {
            if (sLevelSelectStartOption) {
                gCurrentLevel = LEVEL_VENOM_ANDROSS;
            } else if (sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty] == SAVE_SLOT_VENOM_2) {
                gCurrentLevel = LEVEL_VENOM_2;
            }
        } else if ((sCurrentPlanetId == PLANET_AREA_6) && sLevelSelectStartOption) {
            gCurrentLevel = LEVEL_UNK_4;
        }
        Map_LevelStart_AudioSpecSetup(gCurrentLevel);
        sLevelStartState = 0;
        D_menu_801CD968 = 0;
        Map_PlayLevel();
        if ((sLevelSelectStartOption == LEVELSELECT_START_TUNNEL) &&
            ((gCurrentLevel == LEVEL_METEO) || (gCurrentLevel == LEVEL_SECTOR_X) ||
             (sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty] == SAVE_SLOT_VENOM_2))) {
            gLevelPhase = 1;
        }
        // gSavedPathProgress = gPathProgress = zStart;
    }
}

void Map_LevelSelect_Draw(void) {
    static char* sLevelSelectPlanetNames[] = {
        "METEO",  "AREA 6",   "BOLSE",   "SECTOR Z", "SECTOR X", "SECTOR Y", "KATINA", "MACBETH",
        "ZONESS", "CORNERIA", "TITANIA", "AQUAS",    "FORTUNA",  "VENOM 1",  "SOLAR",  "VENOM 2",
    };
    static char* sVenom2StartNames[] = { "NORMAL", "TUNNEL", "ANDROSS" };

    if ((sCurrentPlanetId >= 0) && (sCurrentPlanetId < PLANET_MAX)) {
        RCP_SetupDL(&gMasterDisp, SETUPDL_83);
        gDPSetPrimColor(gMasterDisp++, 0, 0, 255, 255, 0, 255);

        Graphics_DisplaySmallText(20, 200, 1.0f, 1.0f, "PLANET:");
        Graphics_DisplaySmallText(80, 200, 1.0f, 1.0f,
                                  sLevelSelectPlanetNames[sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty]]);

        if (sPlanetArray[sLevelSelectMission][sLevelSelectDifficulty] == SAVE_SLOT_VENOM_2) {
            Graphics_DisplaySmallText(80, 210, 1.0f, 1.0f, sVenom2StartNames[sLevelSelectStartOption]);
        } else if (sLevelSelectStartOption) {
            if ((sCurrentPlanetId == PLANET_SECTOR_X) || (sCurrentPlanetId == PLANET_METEO)) {
                Graphics_DisplaySmallText(80, 210, 1.0f, 1.0f, "WARP ZONE");
            } else if (sCurrentPlanetId == PLANET_VENOM) {
                Graphics_DisplaySmallText(80, 210, 1.0f, 1.0f, "ANDROSS");
            } else if (sCurrentPlanetId == PLANET_AREA_6) {
                Graphics_DisplaySmallText(80, 210, 1.0f, 1.0f, "BETA SB");
            }
        }
    }
}
