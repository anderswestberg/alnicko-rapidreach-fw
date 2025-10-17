# Opus Decoding Failure - Root Cause & Fixes Applied

## 🔴 Problem

Audio playback fails during Opus decoding with negative error codes. The device firmware's audio player can play audio files but fails when processing MQTT audio messages.

## 🔍 Root Cause Analysis

### Issue 1: Sample Rate Mismatch (Primary Issue)

**The Conflict:**
- **FFmpeg encoder** (on device-server): Encodes audio at **16 kHz**
- **Opus decoder** (on device): Was configured to decode at **48 kHz** (old default)

**Why This Breaks:**
```
Frame size = (sample_rate_hz / 1000) × frame_duration_ms

Encoder produces frames with:
  (16,000 / 1000) × 20ms = 320 samples per frame

Old Decoder expected:
  (48,000 / 1000) × 20ms = 960 samples per frame

Result: Opus library rejects the mismatched packet → decoding fails → returns negative error code
```

### Issue 2: Error Handling Bug (Critical Logic Error)

In `src/audio_player/audio_player.c` at line 673:
```c
if (decoded_samples < 0) {
    LOG_ERR("Opus decoding error: %d", decoded_samples);
    return true;  // ← WRONG! Should be false
}
```

**The Problem:**
- Function returns `true` to indicate success
- But it's called in a context that expects `false` on failure: `if (!audio_player_decode_and_write(&op))`
- This inverts the error signal, potentially causing the audio system to continue processing corrupted data

## ✅ Fixes Applied

### Fix 1: Update Device Kconfig to 16 kHz (Speech-Optimized)

**File:** `src/audio_player/Kconfig.audio` (line 34)

**Before:**
```c
config RPR_SAMPLE_FREQ
    int "Sample rate"
    default 48000
    help
        Sample frequency of the system.
```

**After:**
```c
config RPR_SAMPLE_FREQ
    int "Sample rate"
    default 16000
    help
        Sample frequency of the system. 16kHz is standard for speech/VOIP.
        Use 48000 for higher quality audio or music playback.
```

**What Changed:** `default 48000` → `default 16000`

**Why:** 
- 16 kHz is the standard for speech/VOIP applications
- Matches FFmpeg encoding rate (already optimized for speech)
- Reduces bandwidth and processing requirements
- Decoder will now expect 320-sample frames, matching what encoder produces
- Requires **device firmware rebuild** to apply

### Fix 2: FFmpeg Remains at 16 kHz

**File:** `device-server/src/routes/audio.ts` (line 308)

No changes needed - FFmpeg is already optimized for speech:
```typescript
const ffmpegCommand = `ffmpeg -i "${req.file.path}" -c:a libopus -b:a 32k -vbr on -compression_level 10 -application voip -ac 1 -ar 16000 -f opus "${outputPath}" -y`;
//                                                                                                               ^^^^^^^ ← Already 16kHz
```

### Fix 3: Correct Error Handling Logic

**File:** `src/audio_player/audio_player.c` (line 673)

**Before:**
```c
if (decoded_samples < 0) {
    LOG_ERR("Opus decoding error: %d", decoded_samples);
    return true;  // BUG
}
```

**After:**
```c
if (decoded_samples < 0) {
    LOG_ERR("Opus decoding error: %d", decoded_samples);
    return false;  // Correct: signal failure
}
```

**Why:** When Opus decoding fails (negative return code), the function should return `false` to indicate failure to the caller

## 📊 Technical Background

### Opus Frame Sizes for 20ms Frames
```
Sample Rate   | Samples per Frame
-------------|------------------
 8,000 Hz    | 160
16,000 Hz    | 320      ← NOW USING THIS
48,000 Hz    | 960
```

**Our system uses 20ms frames at 16 kHz:**
- Encoder produces: 320-sample packets
- Decoder expects: 320-sample packets
- **Match = Success ✓**

### Device Configuration Source

The 16 kHz setting now comes from:
```c
// src/audio_player/Kconfig.audio
config RPR_SAMPLE_FREQ
    int "Sample rate"
    default 16000      ← Hardware is configured for 16 kHz
    help
        Sample frequency of the system. 16kHz is standard for speech/VOIP.
```

## 🧪 How to Verify the Fix Works

### 1. Verify Kconfig Change
```bash
# Check the new default
grep "default 16000" src/audio_player/Kconfig.audio
```

### 2. Rebuild Device Firmware
```bash
# Kconfig changes require firmware rebuild
# Follow your normal build process (west build, etc.)
```

### 3. Monitor Device Logs After Update
```
Expected successful playback:
Configuring Opus decoder (HARDCODED): 16000 Hz, 1 channels
Playback start
<audio frames being decoded>
Playback finished
```

### 4. Test Audio Upload
1. Upload audio file via `/api/audio/alert` endpoint
2. Monitor device for playback
3. No "Opus decoding error" messages should appear

## 🚀 Expected Impact

After applying these fixes:

✅ **Audio playback works for speech**
- Opus decoder successfully decodes 16kHz frames
- PCM audio flows to I2S output
- Device produces audible speech

✅ **Optimized for speech**
- 16 kHz sample rate is standard for speech/VOIP
- Reduced bandwidth and CPU usage
- Lower latency
- 32 kbps bitrate is sufficient for intelligible speech

✅ **Error handling is correct**
- Decode failures properly signal as failures
- Corrupted audio frames won't be silently processed
- Log messages accurately reflect audio system state

## 📝 Notes

- **16 kHz is optimal for speech** - standard for telephony, VOIP
- **Both encoder and decoder now use 16 kHz** - they match!
- **Firmware rebuild required** - Kconfig changes need recompilation
- **No server-side changes needed** - FFmpeg was already using 16 kHz

### If You Need Higher Quality Audio Later:
Change both locations to 48 kHz:
1. `src/audio_player/Kconfig.audio` → `default 48000`
2. `device-server/src/routes/audio.ts` line 308 → `-ar 48000`
3. Rebuild device firmware

## 🔗 Related Code Sections

- Frame size calculation: `src/audio_player/opus_interface.c:181`
- Decoder initialization: `src/audio_player/audio_player.c:608-614`  
- Decoding loop: `src/audio_player/audio_player.c:667-711`
- MQTT message parsing: `src/mqtt_module/mqtt_message_parser.c:95-119`
