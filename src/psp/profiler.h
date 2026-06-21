#ifndef PSP_PROFILER_H
#define PSP_PROFILER_H

#include "PR/ultratypes.h"

typedef enum {
    PSP_PROFILE_PHASE_GFX_TASK,
    PSP_PROFILE_PHASE_DL_TRAVERSAL,
    PSP_PROFILE_PHASE_G_VTX,
    PSP_PROFILE_PHASE_TRIANGLE,
    PSP_PROFILE_PHASE_CLIPPING,
    PSP_PROFILE_PHASE_TEXTURE_PREPARE,
    PSP_PROFILE_PHASE_TEXTURE_DECODE,
    PSP_PROFILE_PHASE_TEXTURE_UPLOAD,
    PSP_PROFILE_PHASE_BATCH_CONSTRUCTION,
    PSP_PROFILE_PHASE_BATCH_FLUSH,
    PSP_PROFILE_PHASE_PSPGL_SUBMIT,
    PSP_PROFILE_PHASE_GL_FLUSH,
    PSP_PROFILE_PHASE_FINISH_SYNC,
    PSP_PROFILE_PHASE_AUDIO_TASK_DISPATCH,
    PSP_PROFILE_PHASE_AUDIO_SYNTHESIS,
    PSP_PROFILE_PHASE_AUDIO_UPDATE,
    PSP_PROFILE_PHASE_GAME_UPDATE,
    PSP_PROFILE_PHASE_GFX_TASK_BACKPRESSURE,
    PSP_PROFILE_PHASE_VBLANK_WAIT,
    PSP_PROFILE_PHASE_COUNT
} PspProfilePhase;

typedef enum {
    PSP_PROFILE_FLUSH_BUFFER_FULL,
    PSP_PROFILE_FLUSH_TEXTURE_CHANGE,
    PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE,
    PSP_PROFILE_FLUSH_TRANSFORM_CHANGE,
    PSP_PROFILE_FLUSH_CLIPPING_PATH,
    PSP_PROFILE_FLUSH_END_OF_TASK,
    PSP_PROFILE_FLUSH_EXPLICIT_SYNC,
    PSP_PROFILE_FLUSH_OTHER,
    PSP_PROFILE_FLUSH_COUNT
} PspProfileFlushReason;

typedef enum {
    PSP_PROFILE_BATCH_STATE_TEXTURE_ID,
    PSP_PROFILE_BATCH_STATE_TEXTURE_ENV,
    PSP_PROFILE_BATCH_STATE_WRAP_S,
    PSP_PROFILE_BATCH_STATE_WRAP_T,
    PSP_PROFILE_BATCH_STATE_ALPHA_TEST,
    PSP_PROFILE_BATCH_STATE_BLEND,
    PSP_PROFILE_BATCH_STATE_PREMULTIPLIED,
    PSP_PROFILE_BATCH_STATE_COUNT
} PspProfileBatchStateField;

typedef enum {
    PSP_PROFILE_TEXTURE_FLUSH_MATERIAL_KEY,
    PSP_PROFILE_TEXTURE_FLUSH_TEXTURE_ENABLE,
    PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD,
    PSP_PROFILE_TEXTURE_FLUSH_SET_TEXTURE_IMAGE,
    PSP_PROFILE_TEXTURE_FLUSH_COUNT
} PspProfileTextureFlushSource;

void PspProfiler_Init(void);
int PspProfiler_PollControls(u32 rawButtons);
void PspProfiler_StartCapture(void);
void PspProfiler_StopCapture(void);
void PspProfiler_DumpCapture(void);
int PspProfiler_IsCapturing(void);
void PspProfiler_Shutdown(void);
void PspProfiler_DrawStatus(void);
void PspProfiler_RequestExit(void);
int PspProfiler_ExitRequested(void);

#if SF64_PSP_PROFILE_PHASES
void PspProfiler_PhaseBegin(PspProfilePhase phase);
void PspProfiler_PhaseEnd(PspProfilePhase phase);
void PspProfiler_OnGfxTaskComplete(void);
void PspProfiler_CountDisplayListTask(void);
void PspProfiler_CountOpcode(u8 opcode);
void PspProfiler_CountGvtx(u32 count, u32 lit);
void PspProfiler_CountMatrixCommand(u32 projection, u32 composed);
void PspProfiler_CountTriangleCommand(u32 triCount, u32 tri1, u32 tri2);
void PspProfiler_CountTriangleResult(u32 accepted, u32 rejected, u32 clipped, u32 generatedVertices,
                                     u32 outputTriangles);
void PspProfiler_CountTransformWork(u32 vertices, u32 normals, u32 normalizes, u32 lighting, u32 clipCodes,
                                    u32 divides);
void PspProfiler_CountTextureEvent(u32 hit, u32 miss, u32 decode, u32 upload, u32 bytesUploaded);
void PspProfiler_CountBatchFlush(PspProfileFlushReason reason, u32 submittedVertices);
void PspProfiler_CountBatchStateTransitions(int textureIdChanged, int textureEnvChanged, int wrapSChanged,
                                           int wrapTChanged, int alphaTestChanged, int blendChanged,
                                           int premultipliedChanged);
void PspProfiler_CountTextureFlushSource(PspProfileTextureFlushSource source);
void PspProfiler_CountDrawCall(u32 vertices);
void PspProfiler_CountGlFlush(void);
void PspProfiler_CountSync(void);
#else
#define PspProfiler_PhaseBegin(phase) ((void) 0)
#define PspProfiler_PhaseEnd(phase) ((void) 0)
#define PspProfiler_OnGfxTaskComplete() ((void) 0)
#define PspProfiler_CountDisplayListTask() ((void) 0)
#define PspProfiler_CountOpcode(opcode) ((void) 0)
#define PspProfiler_CountGvtx(count, lit) ((void) 0)
#define PspProfiler_CountMatrixCommand(projection, composed) ((void) 0)
#define PspProfiler_CountTriangleCommand(triCount, tri1, tri2) ((void) 0)
#define PspProfiler_CountTriangleResult(accepted, rejected, clipped, generatedVertices, outputTriangles) ((void) 0)
#define PspProfiler_CountTransformWork(vertices, normals, normalizes, lighting, clipCodes, divides) ((void) 0)
#define PspProfiler_CountTextureEvent(hit, miss, decode, upload, bytesUploaded) ((void) 0)
#define PspProfiler_CountBatchFlush(reason, submittedVertices) ((void) 0)
#define PspProfiler_CountBatchStateTransitions(textureIdChanged, textureEnvChanged, wrapSChanged, wrapTChanged, \
                                               alphaTestChanged, blendChanged, premultipliedChanged)            \
    ((void) 0)
#define PspProfiler_CountTextureFlushSource(source) ((void) 0)
#define PspProfiler_CountDrawCall(vertices) ((void) 0)
#define PspProfiler_CountGlFlush() ((void) 0)
#define PspProfiler_CountSync() ((void) 0)
#endif

#endif
