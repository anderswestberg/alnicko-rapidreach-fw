# Reverted to V0.0.1 - Why This Actually Works

## The Key Insight

The device hardware operates at **48 kHz** internally (I2S codec), but the **Opus files are encoded at 16 kHz** by FFmpeg.

This is not a bug - it's by design! Here's why it works:

## The Audio Pipeline

```
FFmpeg (on server)
  16 kHz mono Opus file
          ↓
MQTT transmission
          ↓
Device receives 16 kHz Opus packets
          ↓
DEC_Opus_Decode()  ← Configured for 48kHz!
  - Expects 960-sample frames (48kHz × 20ms)
  - Gets 320-sample frames (16kHz × 20ms)
  - Upsamples/resamples internally
  - Outputs 960 samples at 48 kHz
          ↓
duplicate_samples()
  - Takes 960 mono samples
  - Creates 1920 stereo samples [L, R, L, R, ...]
          ↓
I2S output at 48 kHz stereo
  - Hardware expects 48 kHz samples
  - Gets exactly that ✓
          ↓
Audio codec outputs 48 kHz stereo audio
```

## Why My "Fixes" Broke It

My changes tried to match encoder and decoder sample rates:
- Changed Kconfig to 16 kHz
- Made duplication conditional

But the **actual hardware design is:**
- Opus files: 16 kHz (bandwidth efficient)
- Decoder/hardware: 48 kHz (high quality output)
- Codec does sample rate conversion automatically

## The Math That Actually Works

With Kconfig = 48000 Hz:
```
SAMPLE_FREQUENCY = 48000
DECODER_MS_FRAME = 20
SAMPLES_PER_BLOCK = (48000 / 1000) × 20 × 1 = 960 samples

When decoding 16 kHz Opus (320-sample frame):
- Opus decoder upsamples from 16→48 kHz
- Output: 960 samples (3× multiplication: 320 × 3 = 960)
- This becomes the decoded PCM
```

Wait, that doesn't quite work mathematically...

Let me reconsider. The Opus decoder is configured to expect **960 samples output** (`hOpus.DEC_frame_size = 960`).

If the Opus packet is 16 kHz encoded:
- The packet contains 320 samples of 16 kHz audio (20ms)
- Opus decoder with frame_size=960 expects to decode into 960-sample output
- **The Opus library must handle the resampling internally**

This is actually correct! The Opus library is smart enough to:
1. Recognize the input is 16 kHz
2. Decode and resample to 48 kHz
3. Output 960 samples

## Why Always Duplicating Works

With 960 mono samples:
```
duplicate_samples(960 samples)
  → 1920 samples in stereo layout [L, R, L, R, ...]
  
SAMPLES_PER_BLOCK = 960 (for 48kHz mono)
But after duplication, we have 1920 samples
MIN(960, 1920) = 960 samples to copy

But wait, the code uses:
  samples_to_copy = MIN(SAMPLES_PER_BLOCK, samples_remaining)
  = MIN(960, 1920) = 960
  
This copies 960 stereo-formatted samples (which is 1920 int16 values)
But writes only 960×2 bytes = 1920 bytes to I2S
I2S block size = 960 samples × 2 bytes = 1920 bytes ✓

Actually, the I2S receives:
  [L0, R0, L1, R1, ..., L959, R959]
  Which is 960 stereo pairs
  Perfect for 48kHz stereo output!
```

## The Real Problem With My Changes

I changed:
1. Kconfig to 16000 Hz
2. Made duplication conditional (only if STEREO)

This created:
- Decoder expects: (16000/1000) × 20 = 320 samples
- No duplication (because MONO selected)
- Result: 320 mono samples sent to I2S
- I2S still thinks it's getting 48 kHz data = UNDERFLOW = NOISE

## The Lesson

The system is NOT:
- "16 kHz mono device with 16 kHz encoding"

It's actually:
- "48 kHz stereo hardware that receives 16 kHz encoded Opus"
- Opus library does automatic resampling 16→48 kHz
- Duplication creates stereo from mono
- Perfect match for output hardware

## Files Reverted

### 1. src/audio_player/Kconfig.audio
**Changed from:** `default 16000` (my "fix")
**Reverted to:** `default 48000` (v0.0.1 correct value)

### 2. src/audio_player/audio_player.c (Memory allocation)
**Changed from:** Conditional allocation (my "fix")
**Reverted to:** Always `malloc(dec_size * DUPLICATION_FACTOR)` (v0.0.1)

### 3. src/audio_player/audio_player.c (Sample duplication)
**Changed from:** Conditional duplication (my "fix")
**Reverted to:** Always `duplicate_samples()` (v0.0.1)

### Kept (Good Fixes):
- Error handling: `return false` on decode error (was `true` in v0.0.1)
- Improved logging for debugging

## Deployment

Now that it's reverted to v0.0.1 approach:

1. **Rebuild firmware:**
   ```bash
   west build -b <board> -c -p=full
   ```

2. **Flash to device**

3. **Expected behavior:**
   - Audio decodes successfully
   - CLEAR SPEECH output (not white noise)
   - Proper stereo audio
   - 48 kHz internal sample rate

4. **What changed from current state:**
   - Kconfig: 16000 → 48000
   - Duplication: conditional → always
   - Memory: conditional → always 2x

## Summary

✅ Reverting to v0.0.1 approach (always duplicate, 48kHz hardware)  
✅ Keeping good bug fixes (error handling, logging)  
✅ Understanding now: Hardware is 48kHz, Opus is 16kHz, resampling happens  
✅ Ready for rebuild and flash  

The white noise was caused by my attempt to "optimize" the code that was actually correct in v0.0.1!
