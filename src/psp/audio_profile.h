#ifndef SF64_PSP_AUDIO_PROFILE_H
#define SF64_PSP_AUDIO_PROFILE_H

#include "PR/ultratypes.h"

typedef enum {
    PSP_AUDIO_PROFILE_WAIT_PUBLIC,
    PSP_AUDIO_PROFILE_WAIT_SUBMIT,
    PSP_AUDIO_PROFILE_WAIT_REASON_COUNT,
} PspAudioProfileWaitReason;

#if PSP_AUDIO_PROFILE
void PspAudioProfile_MeBeginJob(u32 commandCount);
void PspAudioProfile_MeBeginCommand(u32 opcode, u32 work);
void PspAudioProfile_MeEndCommand(u32 opcode);
void PspAudioProfile_MeEndJob(void);
void PspAudioProfile_RecordWait(PspAudioProfileWaitReason reason, s32 blocked,
                                u32 elapsedUs);
void PspAudioProfile_RecordCompletion(u32 elapsedUs);
void PspAudioProfile_RecordFallback(void);
void PspAudioProfile_Report(void);
#else
#define PspAudioProfile_MeBeginJob(commandCount) ((void) (commandCount))
#define PspAudioProfile_MeBeginCommand(opcode, work) \
    ((void) (opcode), (void) (work))
#define PspAudioProfile_MeEndCommand(opcode) ((void) (opcode))
#define PspAudioProfile_MeEndJob() ((void) 0)
#define PspAudioProfile_RecordWait(reason, blocked, elapsedUs) \
    ((void) (reason), (void) (blocked), (void) (elapsedUs))
#define PspAudioProfile_RecordCompletion(elapsedUs) ((void) (elapsedUs))
#define PspAudioProfile_RecordFallback() ((void) 0)
#define PspAudioProfile_Report() ((void) 0)
#endif

#endif
