#ifndef PSP_PROFILER_H
#define PSP_PROFILER_H

#include "PR/ultratypes.h"

#ifndef SF64_PSP_PROFILE_COMPONENTS
#define SF64_PSP_PROFILE_COMPONENTS 0
#endif

#ifndef SF64_PSP_PROFILE_TRIVIAL_REJECTS
#define SF64_PSP_PROFILE_TRIVIAL_REJECTS 0
#endif

typedef enum {
    PSP_PROFILE_VERTEX_REUSE_SOURCE_DIRECT,
    PSP_PROFILE_VERTEX_REUSE_SOURCE_GENERIC_UNCLIPPED,
    PSP_PROFILE_VERTEX_REUSE_SOURCE_CLIPPED_ORIGINAL,
    PSP_PROFILE_VERTEX_REUSE_SOURCE_CLIPPED_GENERATED,
    PSP_PROFILE_VERTEX_REUSE_SOURCE_RECTANGLE,
    PSP_PROFILE_VERTEX_REUSE_SOURCE_UNKNOWN,
    PSP_PROFILE_VERTEX_REUSE_SOURCE_COUNT
} PspProfileVertexReuseSource;

typedef enum {
    PSP_PROFILE_PHASE_GFX_TASK,
    PSP_PROFILE_PHASE_DL_TRAVERSAL,
    PSP_PROFILE_PHASE_G_VTX,
    PSP_PROFILE_PHASE_G_VTX_UNPACK,
    PSP_PROFILE_PHASE_G_VTX_MATRIX_PREPARE,
    PSP_PROFILE_PHASE_G_VTX_TRANSFORM,
    PSP_PROFILE_PHASE_G_VTX_POST_TRANSFORM,
    PSP_PROFILE_PHASE_G_VTX_LIGHTING_STAGE,
    PSP_PROFILE_PHASE_G_VTX_LIGHTING_KERNEL,
    PSP_PROFILE_PHASE_G_VTX_ATTRIBUTE_COPY,
    PSP_PROFILE_PHASE_TRIANGLE,
    PSP_PROFILE_PHASE_CLIPPING,
    PSP_PROFILE_PHASE_TEXTURE_PREPARE,
    PSP_PROFILE_PHASE_TEXTURE_DECODE,
    PSP_PROFILE_PHASE_TEXTURE_UPLOAD,
    PSP_PROFILE_PHASE_BATCH_CONSTRUCTION,
    PSP_PROFILE_PHASE_BATCH_FLUSH,
    PSP_PROFILE_PHASE_PSPGL_STATE_SETUP,
    PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD,
    PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_SMALL,
    PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_LARGE,
    PSP_PROFILE_PHASE_PSPGL_SUBMIT,
    PSP_PROFILE_PHASE_PSPGL_SUBMIT_SMALL,
    PSP_PROFILE_PHASE_PSPGL_SUBMIT_LARGE,
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

typedef enum {
    PSP_PROFILE_TEXTURE_CACHE_CI8,
    PSP_PROFILE_TEXTURE_CACHE_RGBA16,
    PSP_PROFILE_TEXTURE_CACHE_RGBA32,
    PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
    PSP_PROFILE_TEXTURE_CACHE_COUNT
} PspProfileTextureCacheClass;

#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
typedef enum {
    PSP_PROFILE_TRI_OUTCOME_DIRECT,
    PSP_PROFILE_TRI_OUTCOME_TRIVIAL_REJECT,
    PSP_PROFILE_TRI_OUTCOME_PARTIAL_CLIP,
    PSP_PROFILE_TRI_OUTCOME_INVALID,
    PSP_PROFILE_TRI_OUTCOME_COUNT
} PspProfileTriOutcome;

typedef enum {
    PSP_PROFILE_TRIVIAL_REJECT_COST_TRIANGLES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_EFFECTIVE_STATE_CALLS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_EFFECTIVE_STATE_RESOLVES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_EFFECTIVE_STATE_REUSES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_EMPTY_BEFORE_STATE,
    PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_NONEMPTY_BEFORE_STATE,
    PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_VERTICES_BEFORE_STATE,
    PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_PREPARE_CALLS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_CACHE_HITS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_CACHE_MISSES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_DECODES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_UPLOADS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_BYTES_UPLOADED,
    PSP_PROFILE_TRIVIAL_REJECT_COST_FLUSHES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_FLUSHED_VERTICES,
    PSP_PROFILE_TRIVIAL_REJECT_COST_EARLY_REJECT_TAKEN,
    PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_BEGINS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_ENDS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_INVALID_NESTING,
    PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_LEAKS,
    PSP_PROFILE_TRIVIAL_REJECT_COST_COUNT
} PspProfileTrivialRejectCost;

typedef enum {
    PSP_PROFILE_TRIVIAL_REJECT_STATE_TEXTURE_ID_OR_REF,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_TEXTURE_ENV,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_WRAP_S,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_WRAP_T,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_ALPHA_TEST,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_BLEND,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_PREMULTIPLIED,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_TEST,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_WRITE,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_FOG_ENABLE_OR_PARAMETERS,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_TRANSFORM_OR_PROJECTION,
    PSP_PROFILE_TRIVIAL_REJECT_STATE_COUNT
} PspProfileTrivialRejectStateField;

typedef enum {
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_TEXTURED,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_UNTEXTURED,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_ALPHA_TEST,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_BLEND,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_DEPTH_TEST,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_DEPTH_WRITE,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_FOG,
    PSP_PROFILE_TRIVIAL_REJECT_RENDER_COUNT
} PspProfileTrivialRejectRenderState;
#endif

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
u64 PspProfiler_RenderPhaseBegin(void);
void PspProfiler_RenderPhaseEnd(PspProfilePhase phase, u64 startUs);
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
void PspProfiler_RecordTextureCacheLookup(PspProfileTextureCacheClass cache, u32 capacity, u32 occupied,
                                          u64 keyHash, u64 baseHash, int hit);
void PspProfiler_RecordTextureCacheInsertion(PspProfileTextureCacheClass cache, u32 capacity, u32 occupied,
                                             u64 keyHash, u64 baseHash);
void PspProfiler_RecordTextureCacheEviction(PspProfileTextureCacheClass cache, u64 keyHash);
void PspProfiler_CountBatchFlush(PspProfileFlushReason reason, u32 submittedVertices);
void PspProfiler_CountBatchStateTransitions(int textureIdChanged, int textureEnvChanged, int wrapSChanged,
                                           int wrapTChanged, int alphaTestChanged, int blendChanged,
                                           int premultipliedChanged);
void PspProfiler_CountTextureFlushSource(PspProfileTextureFlushSource source);
void PspProfiler_CountTrianglePath(u32 directFastpathTriangles, u32 generalPathTriangles,
                                   u32 perspectivePathTriangles, u32 clippedPathTriangles,
                                   u32 directVerticesWritten);
void PspProfiler_CountTri2PairFastpath(u32 hit, u32 invalidVertex, u32 clippedOrRejected,
                                       u32 transformMismatch, u32 directIneligible, u32 bufferPreflush,
                                       u32 validationMismatch);
void PspProfiler_RecordTri2PairValidationMismatch(u32 vertexIndex, u32 fieldMask, u32 batchDelta);
void PspProfiler_CountEffectiveState(u32 resolves, u32 reuses, u32 materialResolves, u32 depthResolves,
                                     u32 fogResolves);
void PspProfiler_CountDrawCall(u32 vertices);
void PspProfiler_CountPspglSubmitSplit(u32 smallDraw, u32 largeDraw, u32 vertices);
void PspProfiler_CountPspglVertexStreamUploadSplit(u32 smallDraw, u32 largeDraw, u32 uploadBytes);
void PspProfiler_CountVertexStream(u32 vboDraw, u32 vertices, u32 upload, u32 uploadBytes, u32 fallbackDraw,
                                   u32 fallbackVertices, u32 pageSwitch, u32 capacityBytes, u32 highWaterBytes,
                                   u32 smallVboDraw, u32 largeVboDraw, u32 smallVboVertices, u32 largeVboVertices);
void PspProfiler_CountTextureWrapRequest(u32 requestS, u32 requestT);
void PspProfiler_CountTextureWrapCall(u32 emittedS, u32 emittedT);
void PspProfiler_CountTextureWrapSkip(u32 skippedS, u32 skippedT);
void PspProfiler_CountTextureParameterCacheMiss(void);
void PspProfiler_CountTextureParameterCacheReplacement(void);
void PspProfiler_CountGlFlush(void);
void PspProfiler_CountSync(void);
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
void PspProfiler_CountTri2OutcomeMatrix(PspProfileTriOutcome first, PspProfileTriOutcome second);
void PspProfiler_CountTrivialRejectCost(PspProfileTrivialRejectCost cost, u32 count);
void PspProfiler_CountTrivialRejectFlush(PspProfileFlushReason reason, u32 submittedVertices);
void PspProfiler_CountTrivialRejectStateTransition(PspProfileTrivialRejectStateField field);
void PspProfiler_CountTrivialRejectRenderState(PspProfileTrivialRejectRenderState state);
#endif
#else
#define PspProfiler_PhaseBegin(phase) ((void) 0)
#define PspProfiler_PhaseEnd(phase) ((void) 0)
#define PspProfiler_RenderPhaseBegin() 0
#define PspProfiler_RenderPhaseEnd(phase, startUs) ((void) (startUs))
#define PspProfiler_OnGfxTaskComplete() ((void) 0)
#define PspProfiler_CountDisplayListTask() ((void) 0)
#define PspProfiler_CountOpcode(opcode) ((void) 0)
#define PspProfiler_CountGvtx(count, lit) ((void) 0)
#define PspProfiler_CountMatrixCommand(projection, composed) ((void) 0)
#define PspProfiler_CountTriangleCommand(triCount, tri1, tri2) ((void) 0)
#define PspProfiler_CountTriangleResult(accepted, rejected, clipped, generatedVertices, outputTriangles) ((void) 0)
#define PspProfiler_CountTransformWork(vertices, normals, normalizes, lighting, clipCodes, divides) ((void) 0)
#define PspProfiler_CountTextureEvent(hit, miss, decode, upload, bytesUploaded) ((void) 0)
#define PspProfiler_RecordTextureCacheLookup(cache, capacity, occupied, keyHash, baseHash, hit) ((void) 0)
#define PspProfiler_RecordTextureCacheInsertion(cache, capacity, occupied, keyHash, baseHash) ((void) 0)
#define PspProfiler_RecordTextureCacheEviction(cache, keyHash) ((void) 0)
#define PspProfiler_CountBatchFlush(reason, submittedVertices) ((void) 0)
#define PspProfiler_CountBatchStateTransitions(textureIdChanged, textureEnvChanged, wrapSChanged, wrapTChanged, \
                                               alphaTestChanged, blendChanged, premultipliedChanged)            \
    ((void) 0)
#define PspProfiler_CountTextureFlushSource(source) ((void) 0)
#define PspProfiler_CountTrianglePath(directFastpathTriangles, generalPathTriangles, perspectivePathTriangles, \
                                      clippedPathTriangles, directVerticesWritten)                            \
    ((void) 0)
#define PspProfiler_CountTri2PairFastpath(hit, invalidVertex, clippedOrRejected, transformMismatch, \
                                          directIneligible, bufferPreflush, validationMismatch)      \
    ((void) 0)
#define PspProfiler_RecordTri2PairValidationMismatch(vertexIndex, fieldMask, batchDelta) ((void) 0)
#define PspProfiler_CountEffectiveState(resolves, reuses, materialResolves, depthResolves, fogResolves) ((void) 0)
#define PspProfiler_CountDrawCall(vertices) ((void) 0)
#define PspProfiler_CountPspglSubmitSplit(smallDraw, largeDraw, vertices) ((void) 0)
#define PspProfiler_CountPspglVertexStreamUploadSplit(smallDraw, largeDraw, uploadBytes) ((void) 0)
#define PspProfiler_CountVertexStream(vboDraw, vertices, upload, uploadBytes, fallbackDraw, fallbackVertices,      \
                                      pageSwitch, capacityBytes, highWaterBytes, smallVboDraw, largeVboDraw,      \
                                      smallVboVertices, largeVboVertices)                                         \
    ((void) 0)
#define PspProfiler_CountTextureWrapRequest(requestS, requestT) ((void) 0)
#define PspProfiler_CountTextureWrapCall(emittedS, emittedT) ((void) 0)
#define PspProfiler_CountTextureWrapSkip(skippedS, skippedT) ((void) 0)
#define PspProfiler_CountTextureParameterCacheMiss() ((void) 0)
#define PspProfiler_CountTextureParameterCacheReplacement() ((void) 0)
#define PspProfiler_CountGlFlush() ((void) 0)
#define PspProfiler_CountSync() ((void) 0)
#endif

#if SF64_PSP_PROFILE_PHASES && SF64_PSP_PROFILE_COMPONENTS
void PspProfiler_ComponentTaskBegin(void);
void PspProfiler_ComponentTaskEnd(void);
void PspProfiler_ComponentMarker(u32 componentId);
u32 PspProfiler_ComponentCurrentId(void);
void PspProfiler_ComponentScopeBegin(u32 componentId);
void PspProfiler_ComponentScopeEnd(void);
void PspProfiler_CountBatchComponentOwnership(u32 ownerComponentId, u32 componentMask, u32 vertices);
void PspProfiler_CountNestedDisplayListCall(void);
#else
#define PspProfiler_ComponentTaskBegin() ((void) 0)
#define PspProfiler_ComponentTaskEnd() ((void) 0)
#define PspProfiler_ComponentMarker(componentId) ((void) 0)
#define PspProfiler_ComponentCurrentId() 0
#define PspProfiler_ComponentScopeBegin(componentId) ((void) 0)
#define PspProfiler_ComponentScopeEnd() ((void) 0)
#define PspProfiler_CountBatchComponentOwnership(ownerComponentId, componentMask, vertices) ((void) 0)
#define PspProfiler_CountNestedDisplayListCall() ((void) 0)
#endif

#endif
