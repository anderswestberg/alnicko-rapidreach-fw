# Audio Chunking Implementation TODO

## Current Issue
- Audio files (~61KB) are sent as single MQTT messages
- Device has limited RAM (640KB total, ~450KB used)
- MQTT buffer can't be larger than ~4KB
- Result: Audio messages are truncated

## Proposed Solution: Chunked Audio Transfer

### Protocol Design
1. **Metadata Message** (topic: `rapidreach/audio/{deviceId}/meta`)
   ```json
   {
     "transfer_id": "uuid",
     "total_chunks": 16,
     "chunk_size": 4096,
     "total_size": 61142,
     "metadata": {
       "priority": 5,
       "volume": 80,
       "filename": "alert.opus"
     }
   }
   ```

2. **Chunk Messages** (topic: `rapidreach/audio/{deviceId}/chunk`)
   ```json
   {
     "transfer_id": "uuid",
     "chunk_index": 0,
     "chunk_data": "base64_encoded_data"
   }
   ```

3. **Completion** (topic: `rapidreach/audio/{deviceId}/complete`)
   ```json
   {
     "transfer_id": "uuid",
     "status": "complete"
   }
   ```

### Implementation Steps

#### Device Server Changes:
1. Split audio into 4KB chunks
2. Send metadata first
3. Send chunks sequentially
4. Send completion message

#### Firmware Changes:
1. Subscribe to three topics (meta/chunk/complete)
2. On metadata: allocate transfer buffer
3. On chunk: write to flash incrementally
4. On complete: trigger playback

## Temporary Workaround
For now, we could:
1. Reduce audio quality/size (lower bitrate)
2. Use shorter audio clips
3. Store common sounds on device and just send IDs
