#include "sys.h"

u64 gDramStack[SP_DRAM_STACK_SIZE64];
u8 gOSYieldData[OS_YIELD_DATA_SIZE];
FrameBuffer gZBuffer; // z buffer
u8 gTaskOutputBuffer[0x30000];
#ifdef TARGET_PSP
u8 gAudioHeap[0xB0000] __attribute__((section(".data"), aligned(64))) = { 1 };
#else
u8 gAudioHeap[0xB0000];
#endif
u16 gTextureRenderBuffer[0x3C40];
u16 gFillBuffer[3 * SCREEN_WIDTH];
FrameBuffer gFrameBuffers[3];
