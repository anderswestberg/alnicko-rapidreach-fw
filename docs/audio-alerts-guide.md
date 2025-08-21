# Audio Alerts Guide

This guide explains how to send audio alerts to RapidReach devices using the web interface or API.

## Overview

The audio alert system allows you to:
- Upload any common audio file format (MP3, WAV, OGG, FLAC, etc.)
- Automatically convert to Opus format for efficient transmission
- Send audio alerts to specific devices via MQTT
- Control playback parameters (volume, priority, repeat count)
- Save audio files to device storage for later playback

## Web Interface

### Using the Audio Alerts Page

1. Navigate to **Audio Alerts** in the web app
2. Select a connected device from the dropdown
3. Click "Select Audio File" and choose your audio file
4. Configure playback options:
   - **Priority**: 0-255 (higher = more important)
   - **Volume**: 0-100%
   - **Play Count**: Number of times to play (0 = infinite)
   - **Interrupt Current**: Stop any currently playing audio
   - **Save to File**: Store on device for future use
5. Click "Send Audio Alert"

## REST API

### POST /api/audio/alert

Send an audio file to a device with automatic Opus encoding.

**Request:**
```bash
curl -X POST http://localhost:3000/api/audio/alert \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -F "audio=@alert.mp3" \
  -F "deviceId=speaker-001" \
  -F "priority=10" \
  -F "volume=90" \
  -F "playCount=1" \
  -F "interruptCurrent=true"
```

**Parameters:**
- `audio`: Audio file (multipart/form-data)
- `deviceId`: Target device ID
- `priority`: 0-255 (default: 5)
- `volume`: 0-100 (default: 80)
- `playCount`: 0-∞ (default: 1, 0=infinite)
- `interruptCurrent`: true/false (default: false)
- `saveToFile`: true/false (default: false)
- `filename`: Optional filename when saving

**Response:**
```json
{
  "success": true,
  "message": "Audio alert sent successfully",
  "details": {
    "deviceId": "speaker-001",
    "topic": "rapidreach/audio/speaker-001",
    "originalFile": "alert.mp3",
    "opusSize": 12384,
    "metadata": {
      "opus_data_size": 12384,
      "priority": 10,
      "volume": 90,
      "play_count": 1,
      "interrupt_current": true
    }
  }
}
```

### POST /api/audio/opus

Send pre-encoded Opus audio directly (no conversion).

**Request:**
```bash
curl -X POST http://localhost:3000/api/audio/opus \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -F "opus=@alert.opus" \
  -F "deviceId=speaker-001" \
  -F "priority=10"
```

## MQTT Message Format

The MQTT message sent to devices contains:
1. JSON header with metadata
2. Binary Opus audio data

**Topic:** `rapidreach/audio/{deviceId}`

**Message Structure:**
```
[JSON Header][Binary Opus Data]
```

**JSON Header Example:**
```json
{
  "opus_data_size": 12384,
  "priority": 10,
  "save_to_file": false,
  "play_count": 1,
  "volume": 90,
  "interrupt_current": true
}
```

## Audio Encoding Settings

The system automatically converts audio to Opus with these settings:
- **Codec**: Opus
- **Bitrate**: 32 kbps (optimized for speech/alerts)
- **VBR**: Enabled for better quality
- **Compression**: Maximum (level 10)
- **Application**: VoIP (optimized for speech)
- **Channels**: Mono
- **Sample Rate**: 16 kHz

For music or high-quality audio, you can manually encode with:
```bash
ffmpeg -i input.mp3 -c:a libopus -b:a 128k -application audio -ar 48000 output.opus
```

## Command Line Tools

### test-audio-encode.sh

Convert audio files to Opus format:
```bash
./device-server/scripts/test-audio-encode.sh input.mp3 output.opus
```

### Send via mosquitto_pub

```bash
# Create JSON header
echo -n '{"opus_data_size":12384,"priority":10,"volume":90}' > header.json

# Combine with Opus data
cat header.json audio.opus > message.bin

# Publish
mosquitto_pub -h localhost -t "rapidreach/audio/speaker-001" -f message.bin
```

## Device Behavior

When a device receives an audio alert:

1. **Parses** the JSON header to extract metadata
2. **Checks priority** - may interrupt current playback
3. **Saves to file** if requested
4. **Sets volume** according to metadata
5. **Plays audio** the specified number of times
6. **Resumes** previous audio if interrupted

## Troubleshooting

### Common Issues

1. **"Failed to convert audio"**
   - Ensure ffmpeg is installed: `sudo apt-get install ffmpeg`
   - Check input file is valid audio

2. **"Device is offline"**
   - Verify device is connected via MQTT
   - Check device appears in device list

3. **"File too large"**
   - Maximum file size is 10MB
   - Use lower bitrate or shorter audio

4. **Audio not playing on device**
   - Check device logs for parser errors
   - Verify Opus decoder is working
   - Ensure volume is not 0

## Best Practices

1. **Use appropriate bitrates:**
   - Speech/Alerts: 32 kbps
   - Music: 128-256 kbps

2. **Keep files small:**
   - Use mono for alerts
   - Lower sample rates for speech

3. **Set priorities wisely:**
   - Emergency: 200-255
   - Important: 100-199
   - Normal: 50-99
   - Low: 0-49

4. **Test locally first:**
   - Use test-audio-encode.sh to verify conversion
   - Monitor MQTT topics during testing
