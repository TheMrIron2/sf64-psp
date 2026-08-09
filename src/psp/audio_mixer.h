#ifndef SF64_PSP_AUDIO_MIXER_H
#define SF64_PSP_AUDIO_MIXER_H

#include "PR/ultratypes.h"
#include "libc/stdbool.h"
#include "PR/abi.h"

typedef u8 uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;
typedef s16 int16_t;
typedef s32 int32_t;
typedef s64 int64_t;

void aClearBufferImpl(uint16_t addr, int nbytes);
void aLoadBufferImpl(const void* source_addr, uint16_t dest_addr, uint16_t nbytes);
void aSaveBufferImpl(uint16_t source_addr, int16_t* dest_addr, uint16_t nbytes);
void aLoadADPCMImpl(int num_entries_times_16, const int16_t* book_source_addr);
void aSetBufferImpl(uint8_t flags, uint16_t in, uint16_t out, uint16_t nbytes);
void aInterleaveImpl(uint16_t left, uint16_t right, uint16_t center, uint16_t lfe, uint16_t surround_left, uint16_t surround_right, uint16_t num_audio_channels);
void aDMEMMoveImpl(uint16_t in_addr, uint16_t out_addr, int nbytes);
void aSetLoopImpl(ADPCM_STATE* adpcm_loop_state);
void aADPCMdecImpl(uint8_t flags, ADPCM_STATE state);
void aResampleImpl(uint8_t flags, uint16_t pitch, RESAMPLE_STATE state);
void aEnvSetup1Impl(uint8_t initial_vol_wet, uint16_t rate_wet, uint16_t rate_left, uint16_t rate_right,
    uint16_t rate_center, uint16_t rate_lfe, uint16_t rate_rear_left, uint16_t rate_rear_right);
void aEnvSetup2Impl(uint16_t initial_vol_left, uint16_t initial_vol_right, int16_t initial_vol_center,
    int16_t initial_vol_lfe, int16_t initial_vol_rear_left, int16_t initial_vol_rear_right);
void aEnvMixerImpl(uint16_t in_addr, uint16_t n_samples, bool swap_reverb, bool x0, bool x1,
                   bool x2, bool x3, uint32_t destinations, uint32_t num_channels, uint32_t cutoff_freq_lfe);
void aMixImpl(uint16_t count, int16_t gain, uint16_t in_addr, uint16_t out_addr);
void aS8DecImpl(uint8_t flags, ADPCM_STATE state);
void aAddMixerImpl(uint16_t count, uint16_t in_addr, uint16_t out_addr);
void aDuplicateImpl(uint16_t count, uint16_t in_addr, uint16_t out_addr);
void aResampleZohImpl(uint16_t pitch, uint16_t start_fract);
void aInterlImpl(uint16_t in_addr, uint16_t out_addr, uint16_t n_samples);
void aFilterImpl(uint8_t flags, uint16_t count_or_buf, int16_t* state_or_filter);
void aHiLoGainImpl(uint8_t g, uint16_t count, uint16_t addr);
void aUnkCmd3Impl(uint16_t a, uint16_t b, uint16_t c);
void aUnkCmd19Impl(uint8_t f, uint16_t count, uint16_t out_addr, uint16_t in_addr);
s32 PspAudioMixer_ExecuteCommandList(const Acmd* commands, s32 commandCount);
s32 PspAudioMixer_ValidateState(void);
void* PspAudioMixer_GetStateAddress(void);
u32 PspAudioMixer_GetStateSize(void);

#endif
