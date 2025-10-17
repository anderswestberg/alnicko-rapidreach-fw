# Audio White Noise Issue - Diagnosis & Fix

## Problem
Audio plays for correct duration but sounds like white noise (random garbage) instead of actual voice/speech.

## Root Cause Analysis

### Issue 1: DUPLICATION_FACTOR Used Incorrectly with Mono Audio
**Location:** `src/audio_player/audio_player.c` lines 648-659

The `duplicate_samples()` function is supposed to convert mono (1 channel) audio to stereo (2 channels) by:
- Taking each mono sample
- Duplicating it to fill left and right channels

**Example:**
```
Input (mono):     [1, 2, 3]
Output (stereo):  [1, 1, 2, 2, 3, 3]  
                   ↑ Left, Right sample pairs
```

**But there's a critical problem:**

```c
DecConfigOpus.pInternalMemory = malloc(dec_size * DUPLICATION_FACTOR);
```

The decoder allocates 2× the normal size to accommodate duplication. **However:**

1. **MONO config:** `NUMBER_OF_CHANNELS = 1`
2. **DUPLICATION_FACTOR = 2** (always - it's not conditional)
3. **SAMPLES_PER_BLOCK = (16000 / 1000) × 20 × 1 = 320**
4. **Expected decode output = 320 samples (mono)**

But the I2S is configured as:
```c
i2s_config->channels = NUMBER_OF_CHANNELS; // = 1 (MONO)
```

So we're:
- Decoding to mono (320 samples)
- Duplicating to stereo layout (640 samples)  
- Sending to I2S configured for MONO (expects 320)
- **Result:** I2S reads stereo-formatted data as mono = CORRUPTED!

### Issue 2: Buffer Overflow During Duplication
If decoder outputs 320 samples and we try to duplicate in-place with index arithmetic:
```c
for (int i = 319; i >= 0; --i) {
    buffer[i * 2]     = buffer[i];  // Write at positions 0, 2, 4, ...
    buffer[i * 2 + 1] = buffer[i];  // Write at positions 1, 3, 5, ...
}
```

The last iteration (i=0):
```
buffer[0 * 2] = buffer[0];      // position 0
buffer[0 * 2 + 1] = buffer[0];  // position 1
```

For i=319 (last original sample):
```
buffer[319 * 2] = buffer[319];       // position 638
buffer[319 * 2 + 1] = buffer[319];   // position 639
```

**This works if buffer is sized for 640 samples**, but then we're sending wrong format to I2S!

## The Real Fix

### Option 1: Disable Stereo Duplication (Recommended for Speech)
Remove the duplicate_samples() call since we're in MONO mode:

```c
// In audio_player_decode_and_write()
// BEFORE:
int16_t *pcm_ptr           = (int16_t *)DecConfigOpus.pInternalMemory;
int      samples_remaining = duplicate_samples(pcm_ptr, decoded_samples);

// AFTER (MONO mode):
int16_t *pcm_ptr           = (int16_t *)DecConfigOpus.pInternalMemory;
int      samples_remaining = decoded_samples;  // No duplication!
// Or wrap in conditional:
#ifdef CONFIG_RPR_AUDIO_PLAYER_STEREO
    int samples_remaining = duplicate_samples(pcm_ptr, decoded_samples);
#else
    int samples_remaining = decoded_samples;  // Mono - no duplication
#endif
```

### Option 2: Fix Memory Allocation
Only allocate extra space if stereo is enabled:

```c
#ifdef CONFIG_RPR_AUDIO_PLAYER_STEREO
    DecConfigOpus.pInternalMemory = malloc(dec_size * DUPLICATION_FACTOR);
#else
    DecConfigOpus.pInternalMemory = malloc(dec_size);  // No extra space needed
#endif
```

## Detailed Problem Flow

```
1. Opus decodes 320 mono samples
2. duplicate_samples() rearranges to stereo layout (640 samples)
3. I2S configured with channels=1 (MONO)
4. memcpy copies 320 bytes to I2S buffer
   ↑ But buffer now has stereo-formatted data mixed with mono format
5. I2S interprets as: [first_half, second_half, ...] instead of [L, R, L, R, ...]
6. Result: Random/corrupted output = White noise!
```

## Testing the Fix

Before rebuild:
1. Confirm in device logs: `NUMBER_OF_CHANNELS = 1` (MONO)
2. Listen to playback: All white noise

After fix:
1. Remove/disable duplication for mono mode
2. Rebuild firmware
3. Listen to playback: Should hear clear voice/speech

## Code Changes Required

### Fix in `audio_player_decode_and_write()`

Replace:
```c
int16_t *pcm_ptr           = (int16_t *)DecConfigOpus.pInternalMemory;
int      samples_remaining = duplicate_samples(pcm_ptr, decoded_samples);
```

With:
```c
int16_t *pcm_ptr = (int16_t *)DecConfigOpus.pInternalMemory;
int      samples_remaining;

#ifdef CONFIG_RPR_AUDIO_PLAYER_STEREO
    // For stereo output: duplicate mono samples to stereo layout
    samples_remaining = duplicate_samples(pcm_ptr, decoded_samples);
#else
    // For mono output: use decoded samples directly
    samples_remaining = decoded_samples;
#endif
```

Or simpler - just remove the duplication entirely since CONFIG says MONO:
```c
int16_t *pcm_ptr           = (int16_t *)DecConfigOpus.pInternalMemory;
int      samples_remaining = decoded_samples;  // No duplication for mono
```

## Why This Causes White Noise Specifically

White noise = random/garbage data because:
1. Stereo-formatted data being read as mono
2. Each "sample" is actually reading across stereo channel boundary
3. No correlation to original audio = sounds like random noise
4. Duration is correct because frame timing is still right

## Verification Steps

1. Check build config: `cat build/.config | grep CONFIG_RPR_AUDIO_PLAYER_STEREO`
   - Should show: `# CONFIG_RPR_AUDIO_PLAYER_STEREO is not set`

2. Apply fix to audio_player.c

3. Rebuild: `west build -b <board> -c -p=full`

4. Test: Upload audio, should hear clear speech instead of noise

## Summary

**Root Cause:** Mono audio is being duplicated for stereo output, but I2S is still configured for mono format, causing data format mismatch and white noise output.

**Solution:** Disable sample duplication when stereo mode is not enabled.

**Files to Modify:** `src/audio_player/audio_player.c` (around line 692-693)
