# MQTT Clients Architecture - Three Independent Clients

## Overview

The RapidReach firmware uses **three separate MQTT clients** for different purposes, each with its own connection, topics, and responsibilities.

---

## 1. Shell MQTT Client 🖥️

**Client ID**: `373334-shell`  
**Implementation**: Zephyr's built-in `SHELL_BACKEND_MQTT`  
**Connection**: Connects to local broker (192.168.2.79) for reliability

### Purpose:
Remote shell access for debugging and device management

### Topics:

**Subscribe to** (Commands from server/user):
```
devices/373334-shell/rx
```

**Publish to** (Shell output):
```
devices/373334-shell/tx
```

### What It Does:
- Provides remote command-line access to the device
- Receives shell commands from device-server or mqtt-terminal
- Sends command output back over MQTT
- Allows remote debugging without physical access

### Example Usage:
```bash
# From mqtt-terminal or device-server
echo "kernel uptime" → devices/373334-shell/rx
                    ← devices/373334-shell/tx (response)
```

### Why Separate:
- **Reliability**: Uses local broker - always accessible for debugging
- **Independence**: Works even if main MQTT fails
- **Security**: Separate credential/topic namespace possible
- **Priority**: Shell commands shouldn't compete with audio data

---

## 2. Main MQTT Client (Speaker) 📡

**Client ID**: `373334-speaker`  
**Implementation**: `src/mqtt_module/mqtt_module.c`  
**Connection**: Tries public broker, falls back to local

### Purpose:
Primary device communication - heartbeats, logs, status, control

### Topics:

**Publish to**:
```
rapidreach/heartbeat/373334-speaker  - Device heartbeat (every 30s)
rapidreach/logs/373334               - Device logs (batched)
```

**Subscribe to**:
```
rapidreach/+/shell/out              - Shell commands (legacy)
(Future: device control topics)
```

### What It Does:
- **Heartbeat**: Keeps device-server informed device is alive
- **Log Publishing**: Sends firmware logs to device-server → MongoDB
- **Status Updates**: Reports firmware version, uptime, IP, etc.
- **Control**: Receives high-level device commands

### Example Data:
```json
{
  "alive": true,
  "deviceId": "373334",
  "seq": 42,
  "uptime": 1234,
  "version": "0.0.8",
  "ip": "192.168.2.67",
  "hwId": "3733343033335114004f004f"
}
```

### Why Separate:
- **Core functionality**: Must be reliable and fast
- **Different QoS needs**: Heartbeats are QoS 0, logs are QoS 1
- **Connection management**: Manages device lifecycle
- **Isolation**: Log flooding doesn't affect audio

---

## 3. MQTT Wrapper Client (Audio Handler) 🔊

**Client ID**: `373334-wrapper`  
**Implementation**: `src/mqtt_module/mqtt_client_wrapper.c` (thread-safe wrapper)  
**Connection**: Tries public broker, falls back to local

### Purpose:
**Audio playback ONLY** - receives and processes audio alerts

### Topics:

**Subscribe to**:
```
rapidreach/audio/373334     - Audio alerts and commands
test/mqtt/wrapper           - Test messages
```

**Publish to**:
```
(None - receive only)
```

### What It Does:
- **Receives audio data**: Large MQTT messages (up to 200KB)
- **Streams to file**: Direct socket-to-filesystem for large files
- **Queues playback**: Adds to audio playback queue
- **Processes metadata**: Volume, priority, play count, interrupt flag

### Why It's Special:
- **Separate thread pool**: Own protocol and worker threads
- **Large message handling**: Can receive 200KB+ audio files
- **Non-blocking**: Doesn't block other MQTT operations
- **Isolated**: Audio processing won't interfere with heartbeats/logs

### Example Message:
```
[4-byte length][JSON metadata][Opus audio data]
```

JSON part:
```json
{
  "opusDataSize": 70858,
  "priority": 1,
  "volume": 25,
  "playCount": 1,
  "interruptCurrent": false,
  "saveToFile": false
}
```

### Why Separate from Main MQTT:
- **Large payloads**: Audio files can be 50-200KB
- **Thread safety**: Parallel processing without blocking
- **Resource isolation**: Heavy file I/O doesn't affect heartbeats
- **Reliability**: If audio fails, main MQTT keeps working
- **Performance**: Dedicated threads for audio streaming

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────┐
│              RapidReach Device                  │
│                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌────────┐│
│  │ Shell MQTT   │  │  Main MQTT   │  │ Wrapper││
│  │ 373334-shell │  │373334-speaker│  │ -audio ││
│  └──────┬───────┘  └──────┬───────┘  └────┬───┘│
│         │                 │                │    │
└─────────┼─────────────────┼────────────────┼────┘
          │                 │                │
          │ (Local IP)      │ (Try public,   │
          │ 192.168.2.79    │  fallback local)
          │                 │                │
          └─────────────────┴────────────────┘
                            │
                      EMQX Broker
                     192.168.2.79
                            │
          ┌─────────────────┴────────────────┐
          │                                  │
    Device Server                      Web App
    (subscribes to                  (triggers audio
     heartbeats, logs)               via device-server)
```

---

## Connection Strategy

### Shell MQTT:
- **Always local**: `192.168.2.79` only
- **Reason**: Must work for debugging even when cloud is down

### Main MQTT:
- **Try public first**: `37.123.169.250` (for when device is outside)
- **Fall back to local**: `192.168.2.79` (3 retries, ~90 seconds)
- **Reason**: Primary communication should work anywhere

### Wrapper MQTT:
- **Smart start**: Uses same broker as Main MQTT
- **Inherits fallback**: Switches if Main switched
- **Reason**: Avoids wasting time on known-bad broker

---

## Why Three Separate Clients?

### Historical Reasons:
1. **Shell MQTT**: Zephyr built-in feature (can't easily modify)
2. **Main MQTT**: Original device communication
3. **Wrapper**: Added later for reliable large-message handling

### Technical Reasons:

**Separation of Concerns**:
- Shell: Interactive debugging
- Main: Device telemetry and control
- Wrapper: Media/data transfer

**Resource Isolation**:
- Audio file I/O doesn't block heartbeats
- Shell commands don't interfere with audio
- Each has dedicated threads and buffers

**Reliability**:
- If audio handler crashes, device stays online (heartbeat continues)
- If main MQTT has issues, shell still works
- Graceful degradation

**Performance**:
- Parallel processing (3 threads handling different data)
- Audio streaming doesn't delay time-critical heartbeats
- Shell responsiveness not affected by audio transfers

---

## Could They Be Combined?

### Theoretically Yes, But:

**Pros of combining**:
- Single connection (saves resources)
- One set of credentials
- Simpler configuration

**Cons of combining**:
- **Complexity**: Need complex internal routing
- **Risk**: One failure affects everything
- **Performance**: Large audio could block heartbeats
- **Threading**: Need careful synchronization
- **Testing**: Harder to isolate issues

### Current Design is Better Because:
1. **Shell MQTT**: Can't change (Zephyr built-in)
2. **Main + Wrapper separation**: Good engineering practice
3. **Resource cost**: Minimal (3 connections ≈ 20KB RAM each)
4. **Reliability**: Isolated failures
5. **Maintainability**: Clear responsibilities

---

## Connection Timeline (In Office - LAN)

```
T+0s:   Boot
T+5s:   Shell MQTT     → 192.168.2.79 ✅ (local only)
T+22s:  Main MQTT      → Try 37.123.169.250 (fails 3x)
T+90s:  Main MQTT      → 192.168.2.79 ✅ (fallback)
T+97s:  Wrapper MQTT   → 192.168.2.79 ✅ (smart start)
```

**All three connected, all functional!**

---

## Troubleshooting

### If Shell MQTT Fails:
- Check local broker: `ping 192.168.2.79`
- Check MQTT port: `nc -zv 192.168.2.79 1883`
- Device can't be debugged remotely

### If Main MQTT Fails:
- Device appears offline in web app
- No heartbeats received
- Logs not uploaded
- **But**: Audio may still work (wrapper independent)

### If Wrapper MQTT Fails:
- Heartbeats still work (device shows online)
- Logs still upload
- **But**: Audio alerts won't work
- Test pings fail

---

## Current Status

✅ **Shell MQTT**: Connected, working  
✅ **Main MQTT**: Connected, publishing heartbeats  
✅ **Wrapper MQTT**: Connected, subscribed to `rapidreach/audio/373334`  
⚠️ **Audio playback**: Decoder error (investigating)

The MQTT architecture is **sound and working correctly**. The audio issue is now isolated to the **decoder initialization**, not the MQTT messaging layer.

---

*Documentation: October 15, 2025*  
*Three clients working as designed*

