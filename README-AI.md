# AI Assistant Guide for RapidReach Firmware Project

This document contains important information, quirks, and hidden details about the RapidReach firmware project that will help AI assistants work effectively with this codebase.

## Project Overview

RapidReach is a smart speaker firmware project using Zephyr RTOS. The project includes MQTT communication, LED control, audio capabilities, and various hardware interfaces.

## Critical Project Quirks and Requirements

### 1. Python Virtual Environment (MANDATORY)
**IMPORTANT**: This project REQUIRES a Python virtual environment to build:
```bash
# The virtual environment is in the project directory:
.venv/

# Activate it BEFORE any west/build commands:
source .venv/bin/activate

# Then you can run:
west build -p -b speaker
west flash

# Note: If you rename the project folder, run this to fix venv paths:
find .venv/bin -type f -exec sed -i '1s|old-folder-name|new-folder-name|g' {} \;
sed -i 's|old-folder-name|new-folder-name|g' .venv/bin/activate*
```

### 2. Board Name
The board name is **`speaker`** (not rapidreach, not rpr, not anything else):
```bash
west build -p -b speaker  # Correct
west build -p -b rapidreach  # WRONG!
```

### 3. MQTT Topic Schema
The device uses hierarchical MQTT topics:
- Main MQTT module:
  - Client ID: `NNNNNN-speaker` (first 6 chars + "-speaker")
  - Heartbeat topic: `rapidreach/heartbeat/{clientId}`
- MQTT Shell Backend:
  - Client ID: `NNNNNN-shell` (first 6 chars + "-shell")
  - Command topic: `devices/{clientId}/rx`
  - Response topic: `devices/{clientId}/tx`
- Device ID is truncated to first 6 characters (e.g., `313938` from full ID)

### 4. Serial Console Access
Device serial console is at `/dev/ttyACM0`:
```bash
# Using screen:
screen /dev/ttyACM0 115200

# Using cat (simpler):
cat /dev/ttyACM0

# Exit screen with: Ctrl+A, then K
```

### 5. Device Network Configuration
After flashing, the device may need DHCP configuration:
```bash
# In device serial console:
net dhcpv4 client start 1  # 1 is the ethernet interface index
```

### 6. MQTT Shell Backend
The MQTT shell backend allows remote CLI access over MQTT. It can conflict with the main MQTT module if both try to connect simultaneously. The configuration is in `boards/speaker.conf`:
```
CONFIG_RPR_MODULE_MQTT=y  # Main MQTT module (heartbeat, etc.)
CONFIG_SHELL_MQTT_BACKEND=y  # MQTT shell backend
```

**Fixed Issues (as of latest update):**
- ✅ Client ID now uses `NNNNNN-shell` format
- ✅ Added retry logic for MQTT connection failures
- ✅ Better handles PHY link speed negotiation events
- ✅ Automatically reconnects after network disruptions

**Usage with mqtt-terminal:**
```bash
# Connect to MQTT shell backend
mqtt-terminal -d "313938-shell" -c "help"
```

### 7. Project Structure
- `src/main/` - Main application code
- `src/modules/` - Feature modules (LED, MQTT, audio, etc.)
- `src/examples/` - Example implementations (Alnicko's work)
- `boards/` - Board-specific configurations
- `external/zephyr/` - Zephyr OS source (for patches)
- `mqtt-terminal/` - Node.js CLI tool for MQTT interaction
- `device-server/` - Node.js REST API bridge to MQTT devices

### 8. Build Output
Build artifacts are in `build/zephyr/`:
- `zephyr.elf` - Main firmware file
- `zephyr.bin` - Binary for flashing
- `zephyr.dts` - Device tree

### 9. Common Commands via MQTT Shell
```bash
# LED control:
led on 0    # Turn on LED 0
led off 0   # Turn off LED 0

# System info:
kernel version
kernel uptime
device list

# Network:
net stats
net iface

# MQTT status:
mqtt status
```

### 10. Docker Services
The project includes a Docker Compose stack:
```bash
# Start all services:
docker compose -f device-servers-compose.yml up -d

# Services included:
# - EMQX (MQTT broker) on port 1883
# - Log Server on port 3000  
# - Device Server on port 3002
```

### 11. Useful Aliases
Add to `~/.bashrc`:
```bash
alias mqtt="node ~/work/alnicko-rapidreach-fw/mqtt-terminal/dist/cli.js"
alias led="node ~/work/alnicko-rapidreach-fw/mqtt-terminal/dist/cli.js -d 313938 -c --quiet"
```

### 12. Patching Zephyr
If you need to modify Zephyr source:
1. Edit files in `external/zephyr/`
2. Create patch: `git diff > patches/my-patch.patch`
3. Patches are applied during build

### 13. Current Device IDs
- Main test device: `313938`
- Full device ID visible in serial console during boot

### 14. Memory IDs for AI Context
- Use only 'Alnicko' and 'Anders Westberg' as users (ID: 6182800)
- DHCP start command needs interface index (ID: 6182797)
- mqtt-terminal alias includes -q flag by default (ID: 6182794)

### 15. HTTP Log Client
The HTTP log client (`src/http_log_client/`) sends device logs to the centralized log server:
- Server runs on port 3001 (via Docker Compose)
- Logs are buffered and sent in batches
- **Chronological ordering**: Filesystem logs (older) are sent before RAM logs (newer)
- No sorting needed - leverages natural FIFO order of both storage types
- Configuration in `prj.conf`:
  ```
  CONFIG_HTTP_LOG_CLIENT=y
  CONFIG_HTTP_LOG_CLIENT_MAX_BATCH_SIZE=50
  CONFIG_HTTP_LOG_CLIENT_DEFAULT_BUFFER_SIZE=500
  ```

## API Keys and Credentials

### EMQX Management API
- Username: `<EMQX_USERNAME>`
- Password: `<EMQX_PASSWORD>`
- Endpoint: `http://localhost:18083/api/v5/`

Example:
```bash
curl -s \
  -u <EMQX_USERNAME>:<EMQX_PASSWORD> \
  -H 'Accept: application/json' \
  http://localhost:18083/api/v5/clients | jq
```

### MQTT Broker Credentials
- Host: `localhost` (or `192.168.2.62` from device)
- Port: `1883`
- Username: `admin`
- Password: `public`

### OpenAI API
- Key: `<OPENAI_API_KEY>` (Store in environment variable)
- Available Models: GPT-3.5, GPT-4, GPT-4o, **GPT-5** (including mini/nano variants), O1
- Note: GPT-5 models don't support custom temperature and use `max_completion_tokens`

### Jira API
- URL: `http://jira:8080`
- Username: `<JIRA_USERNAME>`
- Password: `<JIRA_PASSWORD>`
- Note: May be blocked by CAPTCHA (ID: 6061982)

## Common Issues and Solutions

### Issue: "BOARD is not being defined"
**Solution**: Use `west build -p -b speaker` (not just `west build`)

### Issue: MQTT connection error -116 (ETIMEDOUT)
**Solution**: 
1. Check if EMQX is running: `docker ps`
2. Verify network: `net dhcpv4 client start 1` on device
3. Check for conflicts between main MQTT and shell backend

### Issue: "mqtt: command not found" on device
**Solution**: Ensure `CONFIG_RPR_MODULE_MQTT=y` in `boards/speaker.conf`

### Issue: Device not responding after flash
**Solution**: 
1. Power cycle the device
2. Check serial console for boot messages
3. Run `net dhcpv4 client start 1`

### Issue: MQTT shell subscribes to truncated topic
**Solution**: Check `SH_MQTT_TOPIC_MAX_SIZE` in `external/zephyr/include/zephyr/shell/shell_mqtt.h` - should be `DEVICE_ID_HEX_MAX_SIZE + 12`

## Testing LED Control
Quick test after setup:
```bash
# Using the led alias:
led "led on 0"
sleep 1
led "led off 0"

# Or run the demo:
cd mqtt-terminal
./led-demo.sh
```

## Notes for AI Assistants
1. Always activate the Python virtual environment before building
2. The board name is always "speaker"
3. Check serial console output when debugging connection issues
4. MQTT topics use hierarchical format: `devices/{deviceId}/rx` and `devices/{deviceId}/tx`
5. Device IDs in topics are truncated to 6 characters
6. The project uses Zephyr RTOS - documentation at https://docs.zephyrproject.org/