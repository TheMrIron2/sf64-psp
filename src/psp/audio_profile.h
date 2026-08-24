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
void PspAudioProfile_MeBeginCommand(u32 w0, u32 w1, u32 work);
void PspAudioProfile_MeEndCommand(u32 opcode);
void PspAudioProfile_MeEndJob(void);
void PspAudioProfile_RecordEnvMixer(
    u16 inAddr, u16 samples, u32 flags, u32 destinations, u32 channels,
    u16 volLeft, u16 volRight, u16 volWet, u16 rateLeft, u16 rateRight,
    u16 rateWet);
void PspAudioProfile_RecordWait(PspAudioProfileWaitReason reason, s32 blocked,
                                u32 elapsedUs);
void PspAudioProfile_RecordCompletion(u32 elapsedUs);
void PspAudioProfile_RecordFallback(void);
void PspAudioProfile_Report(void);
#else
#define PspAudioProfile_MeBeginJob(commandCount) ((void) (commandCount))
#define PspAudioProfile_MeBeginCommand(w0, w1, work) \
    ((void) (w0), (void) (w1), (void) (work))
#define PspAudioProfile_MeEndCommand(opcode) ((void) (opcode))
#define PspAudioProfile_MeEndJob() ((void) 0)
#define PspAudioProfile_RecordEnvMixer( \
    inAddr, samples, flags, destinations, channels, volLeft, volRight, \
    volWet, rateLeft, rateRight, rateWet) \
    ((void) (inAddr), (void) (samples), (void) (flags), \
     (void) (destinations), (void) (channels), (void) (volLeft), \
     (void) (volRight), (void) (volWet), (void) (rateLeft), \
     (void) (rateRight), (void) (rateWet))
#define PspAudioProfile_RecordWait(reason, blocked, elapsedUs) \
    ((void) (reason), (void) (blocked), (void) (elapsedUs))
#define PspAudioProfile_RecordCompletion(elapsedUs) ((void) (elapsedUs))
#define PspAudioProfile_RecordFallback() ((void) 0)
#define PspAudioProfile_Report() ((void) 0)
#endif

#endif
