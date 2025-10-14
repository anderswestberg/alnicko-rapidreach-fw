# SNTP Time Synchronization Implementation

## Overview

Successfully implemented SNTP (Simple Network Time Protocol) support for the RapidReach firmware to fix the clock drift issue where device timestamps were 5 minutes ahead of actual time.

## Implementation Summary

### 1. Device Server Fix (Immediate)
- **File**: `device-server/src/services/mqtt-client.ts`
- **Change**: Added timestamp validation that detects future timestamps (>1 minute ahead) and replaces them with current server time
- **Status**: ✅ Deployed to k3s
- **Effect**: Logs now show correct timestamps even while device clock is drifting

### 2. Firmware SNTP Module (Long-term Fix)
- **Created**: Complete SNTP synchronization module for Zephyr firmware
- **Status**: ✅ Built successfully (firmware size: 854KB flash, 574KB RAM)
- **Ready to Flash**: New firmware with SNTP support is ready for deployment

## New Module Structure

```
src/sntp_sync/
├── sntp_sync.h           # Public API
├── sntp_sync.c           # Core SNTP implementation  
├── sntp_sync_shell.c     # Shell commands
├── CMakeLists.txt        # Build configuration
└── Kconfig               # Configuration options
```

## Features

### Automatic Time Synchronization
- Automatically syncs time with NTP servers when network becomes available
- Periodic re-synchronization (default: every 1 hour)
- Falls back to secondary NTP server if primary fails
- Updates both system time and RTC

### NTP Servers Used
- Primary: `pool.ntp.org` (load-balanced NTP pool)
- Secondary: `time.google.com` (Google's public NTP)

### Shell Commands
```bash
# Manual sync
sntp sync [timeout_ms]

# Check status
sntp status

# Enable/disable periodic sync
sntp periodic enable [interval_seconds]
sntp periodic disable
```

## Configuration

### Added to `prj.conf`:
```conf
# SNTP (Simple Network Time Protocol)
CONFIG_SNTP=y
CONFIG_NET_SOCKETS_POSIX_NAMES=y
CONFIG_DNS_RESOLVER=y
CONFIG_DNS_RESOLVER_ADDITIONAL_BUF_CTR=4
CONFIG_DNS_RESOLVER_ADDITIONAL_QUERIES=2
CONFIG_RPR_MODULE_SNTP_SYNC=y
CONFIG_RPR_MODULE_SNTP_SYNC_LOG_LEVEL=3
CONFIG_RPR_MODULE_SNTP_SYNC_AUTO_START=y
CONFIG_RPR_MODULE_SNTP_SYNC_INTERVAL=3600
```

### Kconfig Options
- `CONFIG_RPR_MODULE_SNTP_SYNC`: Enable SNTP module
- `CONFIG_RPR_MODULE_SNTP_SYNC_LOG_LEVEL`: Log level (0-4)
- `CONFIG_RPR_MODULE_SNTP_SYNC_AUTO_START`: Auto-start on network connect
- `CONFIG_RPR_MODULE_SNTP_SYNC_INTERVAL`: Sync interval in seconds (60-86400)

## Integration Points

### 1. Network State Machine
- **File**: `src/init_state_machine/init_state_machine.c`
- **Location**: `state_network_stabilize_entry()` function
- **Behavior**: 
  - Initializes SNTP when network stabilizes
  - Starts periodic sync if auto-start enabled
  - First sync happens 5 seconds after network up

### 2. RTC Module
- **Integration**: SNTP automatically updates RTC after successful sync
- **Function**: `update_rtc_from_system_time()` 
- **Flow**: NTP Server → System Time → RTC

### 3. Build System
- **Added to**: `src/Kconfig.rapidreach`, `src/CMakeLists.txt`, main `CMakeLists.txt`
- **Library**: `lib..__..__src__sntp_sync.a` (linked to app)

## API Functions

```c
// Initialize SNTP sync module
int sntp_sync_init(void);

// Perform one-time sync
int sntp_sync_now(uint32_t timeout_ms);

// Enable/disable periodic sync
int sntp_sync_set_periodic(bool enable, uint32_t interval_seconds);

// Get sync status
int sntp_sync_get_status(int64_t *last_sync_time_out);

// Get last clock drift
int32_t sntp_sync_get_drift(void);
```

## Testing

### Build Status
✅ Firmware compiles successfully
- Total size: 854264 bytes (40.73% of 2MB flash)
- RAM usage: 574388 bytes (87.64% of 640KB)

### To Flash New Firmware
```bash
cd /home/rapidreach/work/alnicko-rapidreach-fw
export PATH=$HOME/.local/bin:/home/rapidreach/zephyrproject/.venv/bin:$PATH
source env-west.sh
west flash
```

### Expected Behavior After Flash

1. **On Boot**:
   - Device waits for network connection
   - 5 seconds after network stabilizes, SNTP initializes
   - First sync attempt within 5 seconds

2. **On Successful Sync**:
   - Logs will show: "SNTP synchronization successful"
   - If clock was drifting: "Time adjusted by X seconds (device was ahead/behind)"
   - RTC updated with correct time
   - Periodic sync scheduled (every 3600 seconds by default)

3. **Periodic Sync**:
   - Re-syncs every hour to prevent drift
   - Runs in background work queue
   - Auto-recovers from failures

### Verification Commands

Once flashed, use these shell commands to verify:

```bash
# Check sync status
uart:~$ sntp status
Last successful sync: 2025-10-14 10:30:00 UTC
Last clock adjustment: 330 seconds (was ahead)
Current system time: 2025-10-14 11:45:23 UTC
RTC time:            2025-10-14 11:45:23

# Manual sync
uart:~$ sntp sync
Synchronizing time with NTP server (timeout: 5000 ms)...
Time synchronized successfully
Clock adjusted by 0 seconds

# Check periodic sync status
uart:~$ sntp status
```

## Benefits

1. **Accurate Timestamps**: All logs and events will have correct time
2. **Automatic Maintenance**: No manual intervention needed
3. **Drift Correction**: Detects and logs clock drift for monitoring
4. **Server Compatibility**: Works with standard NTP servers
5. **Lightweight**: Minimal resource usage

## Troubleshooting

### If SNTP Fails to Sync

1. **Check Network**: Ensure device has internet connectivity
   ```bash
   uart:~$ net ping 8.8.8.8
   ```

2. **Check DNS**: Verify DNS resolution works
   ```bash
   uart:~$ net dns pool.ntp.org
   ```

3. **Manual Sync**: Try manual sync with longer timeout
   ```bash
   uart:~$ sntp sync 10000
   ```

4. **Check Logs**: Look for SNTP-related messages
   ```bash
   uart:~$ log list
   # Look for "sntp_sync" module
   ```

### Common Issues

- **"Failed to resolve"**: DNS not working, check network
- **"SNTP query failed"**: NTP port (123) might be blocked
- **"Timeout"**: Server unreachable or timeout too short

## Future Improvements

1. **Configurable NTP Servers**: Allow user to specify custom NTP servers via config
2. **Fallback List**: Add more fallback NTP servers
3. **Sync on Drift Detection**: Trigger sync when large drift detected
4. **MQTT Time Sync**: Alternative time sync via MQTT broker timestamp

## Related Issues

- **Original Issue**: Device logs were 5+ minutes in the future
- **Root Cause**: RTC was incorrectly set during manufacturing or power loss
- **Server Workaround**: Deployed (detects and corrects future timestamps)
- **Firmware Fix**: This SNTP implementation (prevents drift at source)

## Files Modified

### Firmware
- `prj.conf` - Added SNTP configuration
- `src/Kconfig.rapidreach` - Added SNTP Kconfig
- `src/CMakeLists.txt` - Added SNTP subdirectory
- `src/init_state_machine/init_state_machine.c` - Integrated SNTP startup
- `CMakeLists.txt` - Linked SNTP library
- `src/sntp_sync/*` - New SNTP module (4 files)

### Device Server
- `device-server/src/services/mqtt-client.ts` - Added timestamp validation

## Deployment Status

- ✅ Device Server: Deployed to k3s (timestamp validation active)
- ✅ Firmware: Built successfully, ready to flash
- ⏳ Physical Device: Needs firmware update via `west flash`

## Notes

- SNTP requires working network and DNS
- First sync happens 5 seconds after network stabilizes
- Clock drift is logged but not alarmed (for monitoring)
- Periodic sync prevents gradual drift
- Compatible with NTPv4 protocol

---

**Implementation Date**: October 14, 2025
**Author**: RapidReach Team
**Build**: speaker board firmware v0.0.8+

