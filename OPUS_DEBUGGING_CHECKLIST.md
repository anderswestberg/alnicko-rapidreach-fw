# Opus Decoding - Diagnostic Checklist

Quick reference for diagnosing opus playback issues.

## 🔍 First Steps

### 1. Verify OGG/Opus File Structure
```bash
# Check if file is valid OGG/Opus
file /path/to/audio.opus

# Expected output: "Ogg data, Opus audio"

# If wrong: File encoding failed or corrupted
# Check: FFmpeg logs during encoding, disk space, file permissions
```

### 2. Check Sample Rate Encoding
```bash
# Verify what sample rate was used
ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate \
  -of default=noprint_wrappers=1:nokey=1:noesc=1 audio.opus

# Expected output: 16000
# If 48000: Wrong FFmpeg parameters or Kconfig was changed
```

### 3. Check OGG Header Bytes
```bash
# First 4 bytes should be "OggS" (0x4F676753)
hexdump -C audio.opus | head -n 1

# Expected: 4f 67 67 53 (= "OggS")
# If different: File is not Ogg format
```

## 🧵 Error Codes

### Opus Decoder Error Codes (returned from `opus_decode()`)

Common negative error codes:
```c
#define OPUS_BAD_ARG        -1   // Invalid argument (e.g., frame size mismatch)
#define OPUS_BUFFER_TOO_SMALL -2  // Output buffer too small
#define OPUS_INTERNAL_ERROR -3   // Internal error in library
#define OPUS_INVALID_PACKET -4   // Invalid Opus packet
```

**Most Common:** `-1 (OPUS_BAD_ARG)`
- Usually indicates frame size mismatch
- Check: Decoder sample rate vs encoder sample rate

## 📋 Verification Checklist

### For Each Audio Upload:

- [ ] **File Format Check**
  - [ ] Source file exists and is readable
  - [ ] File is one of supported formats (mp3, wav, flac, etc.)
  
- [ ] **FFmpeg Encoding Check**
  - [ ] FFmpeg command succeeds (exit code 0)
  - [ ] Output file is created
  - [ ] Output file size > 0
  - [ ] File is valid OGG/Opus: `ffprobe` returns "Ogg data, Opus audio"
  - [ ] Sample rate is 48000: `ffprobe -show_entries stream=sample_rate`
  
- [ ] **MQTT Message Check**
  - [ ] JSON header is valid
  - [ ] `opusDataSize` field matches actual opus data length
  - [ ] Total payload size = 4 + JSON length + opus data length
  - [ ] First 4 bytes of opus data are "OggS"
  
- [ ] **Device Decoding Check**
  - [ ] Device receives MQTT message (check logs)
  - [ ] Message parsing succeeds: "Parsed MQTT message:" log appears
  - [ ] File is written to LFS
  - [ ] File is readable by audio player
  - [ ] Decoding succeeds: No "Opus decoding error:" in logs
  - [ ] Audio plays without interruption

## 🐛 Common Failure Modes

### 1. "Opus decoding error: -1"
**Likely Cause:** Frame size mismatch
**Debug Steps:**
1. Check FFmpeg used: `grep "ar.*000" device-server/src/routes/audio.ts`
2. Verify should be 48000 not 16000
3. Check `SAMPLE_FREQUENCY` in device Kconfig
4. If different rates: Update one to match other

### 2. "Opus decoding error: -2"  
**Likely Cause:** Buffer too small
**Debug Steps:**
1. Check buffer size calculations in `audio_player.c`
2. Check `DEC_Opus_getMemorySize()` return value
3. Verify `DecConfigOpus.pInternalMemory` is allocated

### 3. "Invalid Opus header packet"
**Likely Cause:** OGG/Opus container corrupted or incomplete
**Debug Steps:**
1. Verify FFmpeg encoding succeeded (no stderr errors)
2. Check output file is complete: `ls -lh` shows reasonable size
3. Test with `ffplay` locally: `ffplay audio.opus`
4. Check network - may be packet loss during MQTT transmission

### 4. No error message but no audio output
**Likely Cause:** I2S output disabled or audio codec not ready
**Debug Steps:**
1. Check `CONFIG_I2S` is enabled
2. Check codec initialization: Look for codec startup logs
3. Check volume/mute settings
4. Verify I2S hardware is functioning

## 🔧 Configuration to Check

### Device-Side (Firmware)
```c
// src/audio_player/Kconfig.audio
config RPR_SAMPLE_FREQ
    default 16000              // ← Key setting for speech

config RPR_AUDIO_PLAYER_STEREO
    default n                  // Mono: 1 channel
```

### Server-Side (device-server/src/routes/audio.ts)
```typescript
// Line 308 - FFmpeg command
-ar 16000                      // ← Standard for speech/VOIP (matches device Kconfig)
-ac 1                          // ← Mono (1 channel)
-application voip              // ← Speech optimization
-b:a 32k                       // ← Bitrate
```

## 🧪 Test Procedures

### Minimal Reproduction Test
```bash
# 1. Create test audio file (pure mono 48kHz sine wave)
ffmpeg -f lavfi -i "sine=f=440:r=48000:d=2" -ac 1 -ar 48000 -c:a libopus test.opus

# 2. Upload via API
curl -X POST -F "audio=@test.opus" \
  -F "deviceId=<YOUR_DEVICE_ID>" \
  http://device-server/api/audio/opus

# 3. Monitor device logs for errors
```

### Audio Quality Test
```bash
# Listen to encoded file locally
ffplay audio.opus

# If sounds distorted/wrong:
# - Check FFmpeg parameters
# - Check source audio quality
# - Try different bitrate (-b:a 64k or higher)
```

### Packet Analysis
```bash
# Dump first 100 bytes of opus file
hexdump -C audio.opus | head -n 6

# Should see:
# 00000000: 4f 67 67 53 = OggS (magic)
# 00000010: ... page header ...
# 00000020: ... opus header ...
```

## 📊 Performance Notes

### Frame Size Relationship
```
Decoder Configuration:
- Sample Rate: 16000 Hz (speech optimized)
- Frame Duration: 20 ms
- Samples per Frame: 320
- Bytes per Sample: 2 (int16)
- PCM Output per Frame: 640 bytes

After Duplication (DUPLICATION_FACTOR=2 for stereo):
- Output Samples: 640
- Output Bytes: 1280
```

### Memory Implications
```c
#define BLOCK_SIZE (SAMPLES_PER_BLOCK * BYTES_PER_SAMPLE)
// = (16 * 20 * 2) * 2 = 1280 bytes per I2S block

#define BLOCK_COUNT 4  // CONFIG_RPR_I2S_BLOCK_BUFFERS
// Total I2S buffer: 1280 * 4 = 5.12 KB (reduced from 15.36 KB with 48kHz)
```

## 🚨 Emergency Fixes

### Issue: Still Getting Decode Errors After Fix
1. **Verify the fix was applied:**
   ```bash
   grep "ar 48000" device-server/src/routes/audio.ts
   grep "return false" src/audio_player/audio_player.c | grep -A1 "decoded_samples < 0"
   ```

2. **Rebuild/restart services:**
   ```bash
   cd device-server && npm install && npm start
   # Restart device firmware if needed
   ```

3. **Check if multiple sample rates are needed:**
   - Current code only supports one sample rate
   - If you need 16kHz support: Update both encoder and decoder

### Issue: Audio Buffer Overflowing
1. Check I2S timeout value (currently 2000ms)
2. Reduce playback sample rate or bitrate
3. Increase `CONFIG_RPR_I2S_BLOCK_BUFFERS` if available

## 📞 Debug Output to Check

**Successful playback logs should show:**
```
Parsed MQTT message: JSON=122 bytes, Opus=8960 bytes, priority=5
Decoder init: checking if already configured...
Configuring Opus decoder (HARDCODED): 16000 Hz, 1 channels
Playback start
<audio frames being decoded>
Playback finished
```

**Failed playback logs will show:**
```
Opus decoding error: -1
Failed stream init
Cannot parse Opus header
```
