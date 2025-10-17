# OPUS DECODING FAILURE ANALYSIS

## Root Cause Identified: Sample Rate Mismatch

### The Problem

**Encoder (FFmpeg on device-server):**
```typescript
// device-server/src/routes/audio.ts:308
-ar 16000  // Sample rate: 16kHz (16,000 Hz) - optimized for speech/VOIP
```

**Decoder (Zephyr firmware - OLD):**
```c
// src/audio_player/audio_player.c:609
DecConfigOpus.sample_freq = SAMPLE_FREQUENCY;  // Was 48000 Hz (from old Kconfig default)
```

**Old Kconfig Definition:**
```c
// src/audio_player/Kconfig.audio:34
config RPR_SAMPLE_FREQ
    int "Sample rate"
    default 48000    // ← MISMATCH! Encoder does 16kHz, decoder expects 48kHz
```

### Why This Breaks Opus Decoding

The Opus decoder was being initialized with **48kHz** but the encoded audio is at **16kHz**. This causes a critical mismatch:

1. **Frame Size Calculation Error:**
   ```c
   // src/audio_player/opus_interface.c:181
   hOpus.DEC_frame_size = ((uint32_t)(((float)(DEC_configOpus->sample_freq/1000))*DEC_configOpus->ms_frame));
   // Old (48kHz and 20ms): (48 * 20) = 960 samples per frame
   // But the encoded stream has: (16 * 20) = 320 samples per frame
   ```

2. **Opus Decode Failure:**
   ```c
   // src/audio_player/audio_player.c:669-673
   int decoded_samples = DEC_Opus_Decode(op->packet, op->bytes, DecConfigOpus.pInternalMemory);
   if (decoded_samples < 0) {
       LOG_ERR("Opus decoding error: %d", decoded_samples);  // Returns negative error code
       return true;  // BUG: Returns true on error instead of false!
   }
   ```

## Critical Bug in Error Handling

There's also a **critical bug** in the error handling at line 673:
- When `opus_decode()` returns negative (indicating an error), the function returns `true`
- This should return `false` to indicate failure
- The calling code at line 879 checks `if (!audio_player_decode_and_write(&op))` expecting `false` on failure

## Solutions Applied

### Solution 1: Fix Device Kconfig to Use 16 kHz (IMPLEMENTED)

**Change the device's Kconfig to use 16 kHz - standard for speech/VOIP:**

```c
// src/audio_player/Kconfig.audio:32-36
// OLD:
config RPR_SAMPLE_FREQ
    int "Sample rate"
    default 48000

// NEW:
config RPR_SAMPLE_FREQ
    int "Sample rate"
    default 16000  // Match encoder's 16kHz (standard for speech)
    help
        Sample frequency of the system. 16kHz is standard for speech/VOIP.
        Use 48000 for higher quality audio or music playback.
```

**Rationale:** 
- FFmpeg is already encoding at 16kHz for speech (optimal and efficient)
- 16kHz is the industry standard for VOIP and telephony
- Saves bandwidth (half the data vs 48kHz)
- Lower CPU usage for decoding
- Now encoder and decoder will match: both use 16kHz

**Important:** This requires **firmware rebuild** to apply

### Solution 2: Fix Error Handling Bug (IMPLEMENTED)

**In src/audio_player/audio_player.c around line 673:**

```c
// BEFORE (WRONG):
if (decoded_samples < 0) {
    LOG_ERR("Opus decoding error: %d", decoded_samples);
    return true;  // BUG: Should be false!
}

// AFTER (CORRECT):
if (decoded_samples < 0) {
    LOG_ERR("Opus decoding error: %d", decoded_samples);
    return false;  // Correctly signal failure
}
```

### Solution 3 (Alternative, NOT Recommended): Use Dynamic Sample Rate Configuration

Only needed if you want to support multiple sample rates without rebuilding. This is not needed for speech-only use case.

## Error Code Reference

The Opus library error codes are documented in the opus.h header. When `opus_decode()` returns negative:
- Common error codes typically indicate invalid parameters or corrupted data
- The frame size mismatch (960 vs 320 samples) will cause a decode error (usually -1 = OPUS_BAD_ARG)

## Implementation Checklist

- [x] **Fix 1: Update Device Kconfig to 16kHz** (IMPLEMENTED - requires rebuild)
  - [x] Modified `/home/rapidreach/work/alnicko-rapidreach-fw/src/audio_player/Kconfig.audio` line 34
  - [ ] Rebuild device firmware with updated Kconfig
  
- [x] **Fix 2: Fix error handling** (IMPLEMENTED - no rebuild needed)
  - [x] Modified `/home/rapidreach/work/alnicko-rapidreach-fw/src/audio_player/audio_player.c` line 673
  
- [ ] **Optional: Add detailed logging**
  - [ ] Log the frame size calculation at line 613
  - [ ] Log the decoded_samples value before error check
  - [ ] Log packet info (op->bytes, op->packet size)

## Testing Verification

After applying fixes and rebuilding:

1. **Verify Kconfig change:**
   - With 16kHz: (16 * 20) = 320 samples/frame ✓
   - Should match encoded stream expectations

2. **Test audio playback:**
   - Upload test WAV file via `/api/audio/alert`
   - Monitor device logs for "Opus decoding error" - should NOT appear
   - Verify audio plays without errors

3. **Check MQTT packet logs:**
   - Should see OGG header: "4f6767 5330" (OggS)
   - Should show correct opus_data_size in MQTT message
   - Decoder should log: "Configuring Opus decoder (HARDCODED): 16000 Hz, 1 channels"

## Summary of Changes Applied

### Change 1: Device Kconfig Sample Rate Fix
**File:** `src/audio_player/Kconfig.audio` (line 34)
**Change:** `default 48000` → `default 16000`
**Impact:** Device decoder will now expect 16kHz frames, matching FFmpeg encoder
**Requires:** Device firmware rebuild

### Change 2: Error Handling Bug Fix
**File:** `src/audio_player/audio_player.c` (line 673)
**Change:** `return true;` → `return false;`
**Impact:** When Opus decoding fails, function correctly signals failure to caller
**Requires:** No rebuild (though should rebuild together with Kconfig change)
