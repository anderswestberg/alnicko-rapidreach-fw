# Device Reboot Loop Debug Guide

## Symptoms
- Device connects to MQTT broker at 192.168.2.79
- Connection established (client ID: 313938-shell)
- Device disconnects and reboots
- Cycle repeats

## Possible Causes
1. **Memory Issues**: Insufficient heap/stack for new connection
2. **DNS Resolution**: Trying to resolve hostname instead of IP
3. **Certificate/Auth Issues**: MQTT authentication mismatch
4. **Watchdog Timeout**: Operation taking too long
5. **Network Buffer Overflow**: MTU or packet size issues

## Debug Steps

### 1. Connect Serial Console
```bash
# Connect USB cable to device
# Open serial terminal (115200 baud)
picocom -b 115200 /dev/ttyUSB0
# or
screen /dev/ttyUSB0 115200
```

### 2. Capture Boot Logs
Look for:
- Crash messages
- Stack traces  
- "ASSERTION FAILED" messages
- Network initialization errors
- Memory allocation failures

### 3. Try Minimal Firmware
Build with reduced features:
```bash
# In prj.conf, temporarily disable:
# CONFIG_RPR_MODULE_MQTT=n
# CONFIG_SHELL_BACKEND_MQTT=n
west build -p -b speaker
west flash
```

### 4. Check Network Configuration
The device connected successfully, so the IP is correct, but check:
- MTU size conflicts
- DHCP vs static IP
- Gateway configuration

### 5. MQTT Connection Parameters
Current connection from logs:
- Client ID: 313938-shell
- Username: admin
- Clean start: false
- Keepalive: 60
- Session expiry: 7200

## Quick Fixes to Try

### Option 1: Increase Stack Sizes
In prj.conf:
```
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
CONFIG_NET_RX_STACK_SIZE=2048
CONFIG_NET_TX_STACK_SIZE=2048
```

### Option 2: Disable MQTT Temporarily
Comment out in boards/speaker.conf:
```
# CONFIG_RPR_MODULE_MQTT=y
```

### Option 3: Add Debug Output
In prj.conf:
```
CONFIG_LOG_DEFAULT_LEVEL=4
CONFIG_NET_LOG_LEVEL_DBG=y
CONFIG_MQTT_LOG_LEVEL_DBG=y
```

## Rollback Instructions
If needed, revert to old MQTT broker IP:
```bash
git checkout HEAD~1 -- prj.conf boards/speaker.conf src/
west build -p -b speaker
west flash
```
