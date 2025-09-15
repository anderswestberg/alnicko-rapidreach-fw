# MQTT Audio Constraints

## Current Limitations

1. **Device RAM**: Very limited, cannot allocate large buffers
2. **MQTT rx_buffer**: 4KB (defined in mqtt_module.c)
3. **Network TCP window**: 8KB (defined in boards/speaker.conf)
4. **Actual received**: ~100-256 bytes per message

## The Problem

The device server sends audio as: `[JSON header][Binary Opus data]`
- JSON: ~150 bytes
- Opus audio: 3-61KB

But the device can only receive small chunks due to:
- Limited RAM for buffers
- MQTT library constraints
- Network stack limitations

## Solutions Considered

1. **Chunked reading to file** ❌
   - Requires too much RAM for buffers
   - File operations need more API work
   
2. **HTTP download** ❌
   - Requires HTTP client implementation
   - More complex firmware changes

3. **Streaming to audio decoder** ⚠️
   - Would be ideal but requires audio player changes
   - Audio player currently expects complete files

4. **Split MQTT messages** ✅
   - Send audio in multiple small MQTT messages
   - Reassemble on device
   - Works within existing constraints

## Recommended Approach

For now, the device server should:
1. Send metadata in first MQTT message
2. Send audio data in chunks (e.g., 2KB each)
3. Device reassembles and plays

Or use a different protocol for large audio files (HTTP, CoAP, etc.)

