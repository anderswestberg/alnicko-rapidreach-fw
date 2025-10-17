# Comparison: Commit 3c0b007 vs Current Version

## Critical Difference Found!

### Old Version (Commit 3c0b007 - Working):
```c
DecConfigOpus.sample_freq = SAMPLE_FREQUENCY;   // ← HARDCODED
DecConfigOpus.channels    = MONO_CHANNELS;      // ← HARDCODED
```

### Current Version (Broken):
```c
DecConfigOpus.sample_freq = header.input_sample_rate;  // ← FROM FILE HEADER
DecConfigOpus.channels    = header.channels;           // ← FROM FILE HEADER
```

## The Problem

The old version **ignored what was in the file** and used hardcoded SAMPLE_FREQUENCY (48 kHz).

The current version **reads from the file header** (16 kHz) but then tries to allocate memory based on that smaller size, causing buffer overflow.

## Solution

We need to **keep using hardcoded SAMPLE_FREQUENCY** for memory allocation, but we should verify the file matches OR handle the mismatch properly.

The old approach was:
1. Configure decoder for fixed 48 kHz (ignored file header)
2. Allocate 48 kHz buffer size (plenty of space)
3. Opus library handles resampling internally if file is 16 kHz
4. Works because buffer is large enough

## What Should We Do?

### Option 1: Revert to Old Approach (Safest)
Use hardcoded SAMPLE_FREQUENCY like commit 3c0b007:
```c
DecConfigOpus.sample_freq = SAMPLE_FREQUENCY;  // Use 48000
DecConfigOpus.channels    = MONO_CHANNELS;
```

### Option 2: Use File Header BUT Allocate Properly
If using header.input_sample_rate, allocate enough buffer:
```c
// Allocate larger buffer to handle resampling
DecConfigOpus.pInternalMemory = malloc(dec_size * DUPLICATION_FACTOR * 4);
// 4x to be safe for resampling operations
```

### Option 3: Validate & Adapt
```c
// If file is 16kHz but we want 48kHz, allocate for 48kHz
uint32_t decode_freq = (header.input_sample_rate == 16000) ? 48000 : header.input_sample_rate;
DecConfigOpus.sample_freq = decode_freq;
// Allocate based on decode_freq, not header
```

## Recommendation

**Go with Option 1** - Revert to using `SAMPLE_FREQUENCY` (hardcoded). This is what worked in commit 3c0b007 and prevents buffer sizing issues.

The Opus library is smart about resampling - it doesn't matter if the file is 16 kHz when the decoder is configured for 48 kHz; it will handle the conversion automatically.

## Other Improvements in Current Version

Good additions:
- Better logging with printk debug output
- Deinitializing if already configured (not just returning error)
- Using header info for validation/logging (good for debugging)
- Timing measurement improvements

## Files to Change

1. **src/audio_player/audio_player.c** - Revert sample_freq and channels to hardcoded
2. Keep all other improvements (logging, deinitialization, etc.)
