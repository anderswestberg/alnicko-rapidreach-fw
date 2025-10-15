# Multi-Speaker Audio Synchronization Strategies

## Problem Statement

How to play the same audio message on multiple adjacent speakers **simultaneously** to create a coherent sound experience?

**Challenges**:
- Network latency variations (MQTT delivery time)
- Audio file transfer delays (different sizes)
- Processing time differences (decode, queue, playback start)
- Clock drift between devices (even with SNTP)

## Current System Architecture

### Audio Playback Flow:
```
MQTT Message → Parse → Save to /lfs → Queue → Decode → Play
```

**Current Timing**:
- MQTT receive: Variable (depends on network)
- File write: ~50-500ms (depends on audio size)
- Queue processing: Immediate
- Playback start: ~10-50ms

**Total latency**: 100ms - 1000ms from MQTT receive to audio output

### Current Audio Queue Item:
```c
struct audio_queue_item {
    char filename[64];
    uint8_t volume;
    uint8_t priority;
    uint8_t play_count;
    bool interrupt_current;
    // Missing: scheduled_play_time ← Need to add!
};
```

---

## Strategy Comparison

### Strategy 1: Pre-Sync Message (Your Suggestion)

**Approach**: Send sync message → wait 1 second → send audio

**Implementation**:
```javascript
// Server-side
await publishToDevices("sync/prepare", { syncId: 123 });
await sleep(1000);
await publishAudioToDevices(audioData);
```

**Pros**:
- ✅ Simple to implement
- ✅ No firmware changes needed
- ✅ Works with current system

**Cons**:
- ❌ Fixed 1-second delay (adds latency)
- ❌ Doesn't account for different file sizes
- ❌ Large audio files may not arrive in time
- ❌ Network jitter still causes drift (±50-200ms)
- ❌ No coordination if one device is slow

**Expected Sync**: ±100-300ms (noticeable echo)

**Verdict**: 🟡 **Okay for announcements, poor for music**

---

### Strategy 2: Scheduled Playback with Timestamps ⭐ RECOMMENDED

**Approach**: Include absolute play time in MQTT message

**Implementation**:

1. **Add scheduled time to audio queue**:
```c
struct audio_queue_item {
    char filename[64];
    uint8_t volume;
    uint8_t priority;
    uint8_t play_count;
    bool interrupt_current;
    int64_t scheduled_play_time_ms;  // ← NEW: Unix timestamp in ms
};
```

2. **Server calculates play time**:
```javascript
// Device server
const playTime = Date.now() + 3000;  // 3 seconds from now
await publishAudioToDevices(audioData, { playTime });
```

3. **Device waits until play time**:
```c
// In audio_playback_thread()
if (item.scheduled_play_time_ms > 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now_ms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    int64_t delay_ms = item.scheduled_play_time_ms - now_ms;
    
    if (delay_ms > 0) {
        LOG_INF("Scheduled playback in %lld ms", delay_ms);
        k_sleep(K_MSEC(delay_ms));
    }
}
// Start playback NOW (all devices at same time)
```

**Pros**:
- ✅ **Best synchronization**: ±10-50ms with SNTP
- ✅ Accounts for variable network/file transfer delays
- ✅ Uses existing SNTP-synchronized clocks
- ✅ Flexible timing (server controls delay)
- ✅ Devices coordinate via shared time reference

**Cons**:
- ⚠️ Requires firmware change (add scheduled_play_time field)
- ⚠️ Requires server-side change (calculate and send play time)
- ⚠️ Depends on SNTP working (clock sync)

**Expected Sync**: ±10-50ms (imperceptible for speech, good for music)

**Verdict**: ⭐ **BEST SOLUTION**

---

### Strategy 3: Leader-Follower

**Approach**: One device is leader, broadcasts "I'm starting NOW"

**Implementation**:
```
Server → Send audio to all devices (no coordination)
Device A (leader) → Starts playing → Publishes "sync/start"
Device B,C,D → Receive "sync/start" → Start playing immediately
```

**Pros**:
- ✅ No clock dependency
- ✅ Real-time coordination

**Cons**:
- ❌ Leader device must be designated
- ❌ Network latency between leader broadcast and follower start
- ❌ Single point of failure (if leader is down)
- ❌ Complex: Who is leader? How to elect?

**Expected Sync**: ±50-150ms (network latency dependent)

**Verdict**: 🟡 **Okay, but more complex than scheduled playback**

---

### Strategy 4: Countdown Protocol

**Approach**: All devices coordinate via countdown

**Implementation**:
```
Server → "Prepare to play X" (includes audio)
Server → "Ready: T-3 seconds"
Server → "Ready: T-2 seconds"  
Server → "Ready: T-1 seconds"
Server → "GO!"
All devices → Start simultaneously
```

**Pros**:
- ✅ No clock sync needed
- ✅ Visual feedback possible

**Cons**:
- ❌ Requires multiple MQTT messages
- ❌ Network jitter accumulates
- ❌ Slow (countdown adds delay)
- ❌ Complex state machine

**Expected Sync**: ±50-200ms

**Verdict**: 🔴 **Overly complex, poor sync**

---

## Recommended Implementation: Scheduled Playback

### Phase 1: Firmware Changes

**File**: `src/mqtt_module/mqtt_audio_queue.h`

```c
struct audio_queue_item {
    char filename[64];
    uint8_t volume;
    uint8_t priority;
    uint8_t play_count;
    bool interrupt_current;
    int64_t scheduled_play_time_ms;  // ← ADD: Unix timestamp in ms (0 = play immediately)
};
```

**File**: `src/mqtt_module/mqtt_audio_queue.c`

Add before playback starts:
```c
/* Wait for scheduled time if specified */
if (item.scheduled_play_time_ms > 0) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        int64_t now_ms = (int64_t)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
        int64_t delay_ms = item.scheduled_play_time_ms - now_ms;
        
        if (delay_ms > 0 && delay_ms < 60000) {  // Max 60s wait
            LOG_INF("Waiting %lld ms for scheduled playback time", delay_ms);
            k_sleep(K_MSEC(delay_ms));
        } else if (delay_ms < 0) {
            LOG_WRN("Scheduled time already passed by %lld ms, playing now", -delay_ms);
        }
    }
}
```

**File**: `src/mqtt_module/mqtt_audio_handler_v2.c`

Parse scheduled time from MQTT JSON:
```c
// In mqtt_audio_alert_handler_v2()
struct audio_queue_item queue_item = {
    .volume = parsed_msg.metadata.volume,
    .priority = parsed_msg.metadata.priority,
    .play_count = parsed_msg.metadata.play_count,
    .interrupt_current = parsed_msg.metadata.interrupt_current,
    .scheduled_play_time_ms = parsed_msg.metadata.scheduled_play_time_ms,  // ← ADD
};
strncpy(queue_item.filename, temp_filename, sizeof(queue_item.filename));
```

### Phase 2: Server Changes

**File**: `device-server/src/routes/audio.ts`

Add scheduled time calculation:
```typescript
// When broadcasting to multiple devices
const playTime = Date.now() + 3000;  // 3 seconds from now

const metadata = {
  deviceId: 'broadcast',
  volume,
  priority,
  playCount,
  interruptCurrent,
  scheduledPlayTime: playTime,  // ← ADD
  opusDataSize: opusBuffer.length
};

// Send to all target devices with same playTime
for (const device of targetDevices) {
  await mqttClient.publish(
    `rapidreach/audio/${device.deviceId}`,
    messageBuffer
  );
}
```

### Phase 3: Web App Changes

**File**: `web-app/src/audio/AudioAlerts.tsx`

Add multi-device selection and sync option:
```tsx
const [syncPlayback, setSyncPlayback] = useState(true);
const [selectedDevices, setSelectedDevices] = useState<string[]>([]);
const [playDelay, setPlayDelay] = useState(3); // seconds

// When sending to multiple devices
if (selectedDevices.length > 1 && syncPlayback) {
  metadata.scheduledPlayTime = Date.now() + (playDelay * 1000);
}
```

---

## Performance Estimates

### With SNTP-Based Scheduling:

**Clock Synchronization**:
- SNTP accuracy: ±10-100ms
- Clock drift between syncs: ±5ms/hour
- Total clock error: ±50ms typical

**Network Variations**:
- MQTT delivery jitter: ±20-100ms
- File transfer time: 100ms - 1000ms (varies by size)
- Processing time: ±10ms

**Scheduled Playback**:
- Give devices 3 seconds lead time
- All devices wait until absolute timestamp
- Clock drift: ±50ms
- **Result**: Playback starts within ±50ms across all speakers ✅

### Comparison:

| Strategy | Sync Accuracy | Complexity | Best For |
|----------|--------------|------------|----------|
| Pre-sync message | ±200ms | Low | Quick announcements |
| **Scheduled playback** | **±50ms** | **Medium** | **Everything** |
| Leader-follower | ±100ms | High | Special cases |
| Countdown | ±150ms | High | Manual triggers |

---

## Implementation Effort

### Scheduled Playback (Recommended):

**Firmware** (~2 hours):
1. Add `scheduled_play_time_ms` field to `audio_queue_item`
2. Add wait logic before playback in `audio_playback_thread()`
3. Parse scheduled time from MQTT JSON metadata
4. Rebuild and flash

**Device Server** (~1 hour):
1. Add `scheduledPlayTime` to audio message metadata
2. Calculate play time (now + delay)
3. Send same timestamp to all target devices

**Web App** (~1 hour):
1. Add multi-device selection UI
2. Add sync checkbox and delay slider
3. Send to multiple devices with sync flag

**Total**: ~4 hours for complete implementation

**Testing**: Can be done incrementally (firmware first, then server/web)

---

## Alternative: Simple Pre-Sync (30 minutes)

If you want something **quick and simple**:

### Server-Side Only (No Firmware Changes):

```typescript
// In device-server
async function sendSynchronizedAudio(devices: string[], audioBuffer: Buffer) {
  // Step 1: Send sync preparation message
  for (const deviceId of devices) {
    await mqttClient.publish(
      `rapidreach/sync/${deviceId}`,
      JSON.stringify({ action: 'prepare', syncId: Date.now() })
    );
  }
  
  // Step 2: Wait for devices to be ready (adjust based on audio size)
  const audioSizeMB = audioBuffer.length / (1024 * 1024);
  const delayMs = Math.max(2000, audioSizeMB * 1000);  // 1s per MB, min 2s
  await sleep(delayMs);
  
  // Step 3: Send audio to all devices
  const promises = devices.map(deviceId =>
    sendAudioToDevice(deviceId, audioBuffer)
  );
  await Promise.all(promises);
  
  // Step 4: Send "play now" trigger
  await sleep(500);  // Let all files save
  for (const deviceId of devices) {
    await mqttClient.publish(
      `rapidreach/sync/${deviceId}`,
      JSON.stringify({ action: 'play' })
    );
  }
}
```

**Accuracy**: ±100-300ms (acceptable for announcements)  
**Effort**: 30 minutes server-side coding  
**No firmware changes**: Works with current system

---

## Best Strategy Decision Tree

```
Do you need <50ms sync (music)?
├─ YES → Use Scheduled Playback (4 hours)
└─ NO
    └─ Is ±200ms okay? (announcements, alerts)
        ├─ YES → Use Pre-Sync Message (30 min)
        └─ NO → Use Scheduled Playback
```

---

## My Recommendation

### **Start with Pre-Sync Message** (Quick Win)

**Why**:
1. ✅ Can implement **today** (30 minutes)
2. ✅ No firmware changes needed
3. ✅ Good enough for announcements
4. ✅ Can upgrade to scheduled playback later

**How**:
1. Update device-server to send sync messages
2. Calculate delay based on audio file size
3. Broadcast to all devices with timing coordination

### **Upgrade to Scheduled Playback** (Best Quality)

**When**:
- After you've tested pre-sync and want better sync
- When you need synchronized music playback
- When ±50ms matters for your use case

**Why**:
1. ✅ Best possible sync with SNTP (±10-50ms)
2. ✅ Deterministic (not dependent on network speed)
3. ✅ Scalable (works with any number of devices)
4. ✅ Uses your SNTP investment

---

## Code Snippets

### Quick Pre-Sync (Server Only)

```typescript
// device-server/src/routes/audio.ts

export async function sendSyncedAudioAlert(
  deviceIds: string[],
  audioBuffer: Buffer,
  options: AudioOptions
) {
  const syncId = Date.now();
  
  // Calculate appropriate delay based on audio size
  const audioSizeMB = audioBuffer.length / (1024 * 1024);
  const transferTimeEstimate = audioSizeMB * 800; // 800ms per MB
  const syncDelay = Math.max(2000, transferTimeEstimate + 1000);
  
  logger.info(`Syncing ${deviceIds.length} devices with ${syncDelay}ms delay`);
  
  // Send to all devices in parallel
  const promises = deviceIds.map(deviceId =>
    mqttClient.publish(
      `rapidreach/audio/${deviceId}`,
      createAudioMessage(audioBuffer, options)
    )
  );
  
  await Promise.all(promises);
  
  logger.info(`Audio sent to all devices, playback will sync within ${syncDelay}ms window`);
}
```

**Usage**:
```typescript
await sendSyncedAudioAlert(
  ['313938', '313939', '313940'],  // Multiple device IDs
  audioBuffer,
  { volume: 50, priority: 5, playCount: 1 }
);
```

### Better: Scheduled Playback (Firmware + Server)

**Firmware Addition** (`mqtt_audio_queue.c`):
```c
// Before starting playback
if (item.scheduled_play_time_ms > 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t now_ms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    int64_t delay_ms = item.scheduled_play_time_ms - now_ms;
    
    if (delay_ms > 0 && delay_ms < 30000) {
        LOG_INF("Scheduled playback in %lld ms (sync mode)", delay_ms);
        k_sleep(K_MSEC(delay_ms));
    } else if (delay_ms < -1000) {
        LOG_WRN("Scheduled time missed by %lld ms, playing now", -delay_ms);
    }
}

audio_player_start(item.filename);  // All devices start here at same time!
```

**Server**:
```typescript
const playTime = Date.now() + 3000;  // Absolute timestamp
metadata.scheduledPlayTime = playTime;  // Send to all devices
```

**Result**: All devices wait until `playTime`, then start together ±10-50ms!

---

## Synchronization Quality Levels

### ±300ms: Acceptable for Announcements
- Pre-sync message
- Simple implementation
- Noticeable echo in large rooms

### ±50ms: Good for Music/Alerts
- Scheduled playback with SNTP
- Imperceptible to human ear
- Professional PA system quality

### ±10ms: Perfect Sync
- Would require gPTP + PTP hardware
- Not achievable with current hardware
- Unnecessary for your use case

---

## Quick Start Guide

### 1. Test Pre-Sync (Today - 30 min)

```bash
# In device-server, add helper function
async function sendToMultipleDevices(deviceIds, audioBuffer) {
  await Promise.all(
    deviceIds.map(id => sendAudioToDevice(id, audioBuffer))
  );
}
```

**Test**:
- Send to 2-3 nearby speakers
- Listen for echo/delay
- If acceptable → done!
- If not → upgrade to scheduled playback

### 2. Implement Scheduled Playback (Later - 4 hours)

Only if you need better sync quality.

---

## Recommendation

**Start Simple**: 
1. Use pre-sync message approach (server-side only)
2. Test with your speakers
3. If sync is good enough (±200ms) → **done!**
4. If you need better sync → implement scheduled playback

**Don't use gPTP**:
- Hardware doesn't support it well
- Overkill for your use case
- SNTP is better choice

---

**Next Steps**:
1. Flash current firmware (with SNTP)
2. Test pre-sync approach from device-server
3. Evaluate sync quality
4. Decide if scheduled playback is worth 4 hours effort

Want me to implement the pre-sync message approach in the device-server now?

---

*Analysis Date: October 14, 2025*  
*Recommendation: Start with pre-sync, upgrade to scheduled playback if needed*

