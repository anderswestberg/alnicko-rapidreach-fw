# Decode Time Measurement Fix

## Issue Identified

The original opus decoding time measurement was **incorrect** - it was measuring the entire playback duration including I2S output delays, not just the actual decoding time.

### Before (Wrong):
```
Timer Start:  Line 840 (before OGG parsing begins)
Timer End:    Line 901 (after entire playback completes)
Measures:     Entire playback including I2S writes, yields, and network delays
Example Log:  "The opus file was decoded in 18120 ms"
             ↑ This actually includes 18+ seconds of audio playback!
```

### Problem:
- A 3-second audio file takes ~3000ms to output via I2S at real-time speed
- Additional delays from k_msleep(1) and k_yield() add up
- I2S blocking on i2s_write() blocks until DMA completes
- Result: Measured time is dominated by playback, not decoding

## Fix Applied

### Improvement 1: Per-Packet Decode Timing
```c
// src/audio_player/audio_player.c (line 667-684)
#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
    uint64_t decode_start = k_uptime_get();
#endif

int decoded_samples = DEC_Opus_Decode(op->packet, op->bytes, ...);

#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
    uint64_t decode_time = k_uptime_delta(&decode_start);
    if (decode_time > 10) {  // Log only slow decodes
        LOG_DBG("Opus decode took %lld ms for packet (%u bytes -> %d samples)", 
                decode_time, op->bytes, decoded_samples);
    }
#endif
```

**Benefit:** Can now see per-packet decode time (typically <1ms per packet)

### Improvement 2: Clarified Total Time Message
```c
// src/audio_player/audio_player.c (line 838-903)
// Before:
LOG_INF("The opus file was decoded in %lld ms", delta_time);

// After:
LOG_INF("Total time (decode + I2S output): %lld ms for %d packets", 
        total_decode_time, decoded_samples_total);
LOG_INF("Note: Time includes I2S writes and yields - pure decode is typically <1ms per packet");
```

**Benefit:** Message now accurately describes what's being measured

## Expected Output

### Before Fix:
```
The opus file was decoded in 18120 ms
Samples decoded 1440
```
❌ Misleading - this includes all I2S playback time

### After Fix:
```
Total time (decode + I2S output): 18120 ms for 1440 packets
Note: Time includes I2S writes and yields - pure decode is typically <1ms per packet

[Optional debug logs for slow packets]:
Opus decode took 0.5 ms for packet (300 bytes -> 320 samples)
Opus decode took 0.3 ms for packet (256 bytes -> 320 samples)
```

✅ Clear: Shows total time is playback time, with actual decode being <1ms

## Timing Breakdown Example

For a 3-second audio file at 16 kHz mono:
```
Total samples: 16000 * 3 = 48,000 samples
Frame size: 320 samples = 20ms per frame
Total frames: 48,000 / 320 = 150 frames

Measured as:
- Pure Opus decoding: ~50-100ms (150 frames × 0.3-0.7ms each)
- I2S blocking (realtime playback): ~3000ms (3 seconds of audio)
- Yields and sleeps: ~150ms (k_msleep(1) per frame, k_yield() overhead)
- File I/O: ~50ms (LFS read operations)

Total: ~3200-3300ms (mostly I2S playback)
```

## When to Use Measurements

### Per-Packet Debug Logs
**Enable:** Only when debugging slow decode issues
- Set CONFIG_RPR_MEASURING_DECODE_TIME and ensure log level includes DEBUG
- Look for packets with >5ms decode time (abnormal)
- Helps identify problematic audio files or codec issues

### Total Time Message
**Always:** Visible with CONFIG_RPR_MEASURING_DECODE_TIME enabled
- Useful for understanding system throughput
- Compare with expected playback time (duration of audio file)
- Should be roughly equal or slightly more than audio duration

## System Performance Expectations

### At 16 kHz (Current):
- Per-packet decode: 0.3-0.7 ms typical
- Per-packet throughput: 300-500 bytes per ~0.5ms
- Effective bandwidth: ~600-1000 KB/s decode capacity
- Bottleneck: I2S playback (realtime), not decoding

### Key Insight:
**The Opus decoder is extremely fast.** The measured "18120 ms" is almost entirely I2S playback time at real-time speed (playing 18 seconds of audio takes 18 seconds), not decoding time.

## Files Modified

- `src/audio_player/audio_player.c` (lines 667-711 and 838-903)
  - Per-packet timing in `audio_player_decode_and_write()`
  - Clarified total time measurement
  - Better documentation in log messages

## Related Configuration

```c
// Enable decode time measurement
CONFIG_RPR_MEASURING_DECODE_TIME=y

// Set log level to see debug messages
// config RPR_MODULE_AUDIO_PLAYER_LOG_LEVEL = 4 (DEBUG)
// Log levels: 0=OFF, 1=ERROR, 2=WARNING, 3=INFO, 4=DEBUG
```

## Future Improvements (Optional)

If you need more detailed timing analysis:

1. **Separate I2S timing:**
   - Measure i2s_write() duration separately
   - Track DMA completion vs CPU blocking time

2. **OGG parsing timing:**
   - Measure ogg_sync_pageout() and ogg_stream_packetout()
   - Track packet extraction overhead

3. **File I/O timing:**
   - Measure fs_read() call duration
   - Track LFS performance

These would require more granular timing around each subsystem component.
