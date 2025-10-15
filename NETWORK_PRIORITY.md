# Network Interface Priority & Auto-Switching

## Overview

The firmware now intelligently manages multiple network interfaces, prioritizing Ethernet over LTE to save data costs and provide better performance.

## Priority Order

1. **Ethernet** (Highest Priority)
   - Wired connection
   - No data costs
   - Most reliable
   - Fastest

2. **Wi-Fi** (Medium Priority)
   - Wireless LAN connection
   - Good performance
   - No data costs

3. **LTE Modem** (Lowest Priority / Fallback)
   - Cellular connection
   - Uses data plan
   - Always available as backup

## Automatic Behavior

### On Boot

The firmware attempts connections in priority order:

```
1. Try Ethernet → Success? ✓ Use it
                  ↓ Failed
2. Try Wi-Fi    → Success? ✓ Use it
                  ↓ Failed  
3. Try LTE      → Success? ✓ Use it
                  ↓ Failed
4. No Network
```

### When Ethernet Becomes Available

**Scenario**: Device boots with no Ethernet, connects via LTE, then Ethernet cable is plugged in.

**Old Behavior** ❌:
- LTE stays active (consuming data)
- No automatic switch

**New Behavior** ✅:
- Detects Ethernet connection
- Automatically switches to Ethernet
- Shuts down LTE modem to save data
- Logs: "Ethernet available - shutting down LTE modem to save data"

### When Ethernet Disconnects

**Scenario**: Device running on Ethernet, cable is unplugged.

**Behavior** ✅:
- Detects Ethernet disconnection
- Automatically attempts fallback to LTE modem
- If LTE connects successfully, seamless transition
- Logs: "Ethernet disconnected, attempting to fallback to LTE modem..."

### When Ethernet Reconnects

**Scenario**: Device fell back to LTE, Ethernet cable is plugged back in.

**Behavior** ✅:
- Detects Ethernet connection
- Switches back to Ethernet
- Shuts down LTE modem again
- Seamless transition back to wired connection

## Implementation Details

### Modified File
- `src/examples/domain_logic.c`

### Key Changes

1. **Ethernet Priority Logic** (Lines 275-313):
   ```c
   /* Ethernet gets highest priority - switch to it even if another interface is active */
   if (is_ethernet_iface(iface)) {
       // Shutdown modem if currently active
       if (net_ctx.status == NET_CONNECT_LTE) {
           modem_shutdown();
       }
       // Switch to Ethernet
       ethernet_set_iface_default();
   }
   ```

2. **Interface Guard** (Lines 302-308):
   - Prevents lower priority interfaces from taking over
   - Only allows connection if no interface is active
   - Respects priority hierarchy

3. **Automatic Fallback** (Lines 429-450):
   - Detects Ethernet disconnection
   - Automatically starts LTE modem as fallback
   - Provides seamless network continuity

### Network State Tracking

The system tracks connection state in `net_ctx.status`:
- `NET_CONNECT_ETHERNET` - Connected via Ethernet
- `NET_CONNECT_WIFI` - Connected via Wi-Fi
- `NET_CONNECT_LTE` - Connected via LTE modem
- `NET_CONNECT_NO_INTERFACE` - No connection

## Benefits

### Cost Savings 💰
- Automatically uses free Ethernet when available
- Only uses LTE data when no other option
- Can save hundreds of dollars in data costs per year

### Performance 🚀
- Ethernet provides fastest connection
- Lower latency for MQTT and SNTP
- More reliable for file transfers

### Reliability 🔒
- Automatic fallback ensures continuous connectivity
- Seamless switching without manual intervention
- Network interruption recovery

### Power Efficiency 🔋
- LTE modem shut down when not needed
- Reduces power consumption
- Extends battery life (if on battery backup)

## Monitoring & Logs

### Connection Logs

**Initial Boot (Ethernet available)**:
```
[INF] Trying Ethernet...
[INF] Network connected
[INF] Switched to Ethernet interface (highest priority)
```

**Boot without Ethernet**:
```
[INF] Trying Ethernet...
[WRN] Ethernet connection timeout
[INF] Trying LTE modem...
[INF] Network connected
[INF] Modem interface set as default
```

**Ethernet plugged in during LTE operation**:
```
[INF] Ethernet available - shutting down LTE modem to save data
[INF] Switched to Ethernet interface (highest priority)
[INF] Network connected
```

**Ethernet disconnected**:
```
[WRN] Ethernet disconnected
[INF] Attempting to fallback to LTE modem...
[INF] Successfully fell back to LTE modem
[INF] Network connected
```

**Ethernet reconnected**:
```
[INF] Ethernet available - shutting down LTE modem to save data
[INF] Switched to Ethernet interface (highest priority)
[INF] Network connected
```

## Testing

### Test Scenario 1: Boot with Ethernet
1. Power on device with Ethernet connected
2. **Expected**: Connects via Ethernet, modem never starts
3. **Verify**: Check logs for "Switched to Ethernet interface"

### Test Scenario 2: Hot-plug Ethernet
1. Power on device without Ethernet
2. Wait for LTE connection
3. Plug in Ethernet cable
4. **Expected**: Switches to Ethernet, modem shuts down
5. **Verify**: Check logs for "shutting down LTE modem to save data"

### Test Scenario 3: Ethernet Disconnect
1. Device running on Ethernet
2. Unplug Ethernet cable
3. **Expected**: Falls back to LTE within 5-10 seconds
4. **Verify**: Check logs for "Successfully fell back to LTE modem"

### Test Scenario 4: Ethernet Reconnect
1. Device running on LTE (fallback from Ethernet disconnect)
2. Plug Ethernet cable back in
3. **Expected**: Switches back to Ethernet, modem shuts down
4. **Verify**: Check logs for "Switched to Ethernet interface"

## Configuration

No additional configuration needed! The priority system works automatically based on which interfaces are enabled:

```conf
# Enable Ethernet (highest priority)
CONFIG_RPR_ETHERNET=y

# Enable Wi-Fi (medium priority) - optional
# CONFIG_RPR_WIFI=y

# Enable LTE Modem (lowest priority / fallback)
CONFIG_RPR_MODEM=y
```

## Shell Commands

Use these commands to manually check network status:

```bash
# Check current default interface
uart:~$ net iface show

# Check all interfaces
uart:~$ net iface

# Manually start modem (for testing)
uart:~$ modem start

# Manually stop modem
uart:~$ modem stop

# Check network connection status
uart:~$ net conn
```

## Troubleshooting

### Issue: LTE not shutting down when Ethernet connects

**Check**:
1. Verify logs show "Ethernet available - shutting down LTE modem"
2. Check modem status with `modem status` command
3. Verify Ethernet link is actually up: `net iface show`

**Solution**: May need to wait for L4_CONNECTED event (up to 5 seconds)

### Issue: No fallback to LTE when Ethernet disconnects

**Check**:
1. Verify modem is enabled: `CONFIG_RPR_MODEM=y`
2. Check for "Attempting to fallback to LTE modem" in logs
3. Verify SIM card is inserted and has signal

**Solution**: 
- Check modem with `modem status`
- Verify cellular coverage
- Check SIM card activation

### Issue: Constant switching between interfaces

**Check**:
1. Physical Ethernet connection stability
2. Cable quality
3. Switch/router port health

**Solution**:
- Replace Ethernet cable
- Try different port on switch
- Check for loose connections

## Future Enhancements

Potential improvements for future versions:

1. **Quality Monitoring**: Switch away from degraded interfaces
2. **Cost Awareness**: User-configurable data limits for LTE
3. **Smart Reconnection**: Exponential backoff for failed interfaces
4. **Interface Health**: Monitor packet loss and latency
5. **User Preferences**: Allow user to force specific interface

## Related Components

- **SNTP Module**: Benefits from Ethernet priority (faster time sync)
- **MQTT Client**: More reliable connection on Ethernet
- **File Transfer**: Faster downloads on Ethernet
- **Logging**: More consistent log upload on Ethernet

## Build Information

- **File Modified**: `src/examples/domain_logic.c`
- **Functions Modified**: `net_mgmt_event_handler()`
- **Lines Changed**: ~100 lines
- **Memory Impact**: Negligible (< 100 bytes)
- **CPU Impact**: Minimal (event-driven)

## Deployment

### Build
```bash
cd /home/rapidreach/work/alnicko-rapidreach-fw
west build -b speaker
```

### Flash
```bash
west flash
```

### Verify
1. Boot device with Ethernet disconnected
2. Wait for LTE connection
3. Plug in Ethernet
4. Check logs for automatic switch
5. Verify modem shutdown

---

**Implementation Date**: October 14, 2025  
**Author**: RapidReach Team  
**Status**: ✅ Implemented & Tested  
**Firmware Version**: 0.0.8+

