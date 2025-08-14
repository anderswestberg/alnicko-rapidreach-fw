# MQTT Terminal for RapidReach

An interactive terminal application that provides remote CLI access to RapidReach devices via MQTT.

## Features

- 🚀 Interactive terminal with command history and tab completion
- 📡 Remote device control via MQTT
- 🎨 Colorized output for better readability
- ⚡ Single command execution mode
- 🔧 Configurable via environment variables or command line
- 📝 Full command help system

## Installation

```bash
# Clone or copy the mqtt-terminal directory
cd mqtt-terminal

# Install dependencies
npm install

# Build the TypeScript code
npm run build
```

## Configuration

### Environment Variables

Copy `env.example` to `.env` and configure:

```bash
cp env.example .env
```

Edit `.env`:
```env
MQTT_BROKER_HOST=192.168.2.62
MQTT_BROKER_PORT=1883
DEVICE_ID=rapidreach_device
```

### Command Line Options

```
Options:
  -V, --version            output the version number
  -h, --host <host>        MQTT broker host (default: from .env)
  -p, --port <port>        MQTT broker port (default: from .env)
  -d, --device <id>        Device ID (default: from .env)
  -u, --username <username> MQTT username
  -P, --password <password> MQTT password
  -c, --command <cmd>      Execute single command and exit
  --timeout <ms>           Response timeout in milliseconds (default: 5000)
  --help                   display help for command
```

## Usage

### Starting the Terminal

**Important:** You must provide the device ID when connecting. The device uses only the first 6 characters of the device ID for MQTT topics.

#### Finding Your Device ID

1. **From EMQX Dashboard** (easiest method):
   - Navigate to http://localhost:18083 (login: admin/public)
   - Go to "Clients" section
   - Look for your device - it will show as a 6-character hex ID (e.g., `313938`)
   - This is the client ID you use with mqtt-terminal

2. From device boot logs:
   ```
   [00:00:00.352,000] <inf> dev_info: Device ID detected: 313938343233510e003d0029
   ```

3. From MQTT shell initialization logs:
   ```
   [00:14:50.469,000] <inf> shell_mqtt: Logs will be published to: 313938_tx
   [00:14:50.469,000] <inf> shell_mqtt: Subscribing shell cmds from: 313938_rx
   ```

4. Via serial console:
   ```
   rapidreach> device id
   ```

### Interactive Mode

```bash
# Simple usage - only device ID is required!
npm run start -- -d 313938

# Or with full device ID
npm run start -- -d 313938343233510e003d0029

# Custom broker (if not using localhost)
npm run start -- -d 313938 -h 192.168.2.62

# All options have sensible defaults:
# - host: localhost
# - port: 1883
# - username: admin
# - password: public
# - timeout: 5000ms
```

### Single Command Mode

Execute a single command and exit - perfect for scripting and automation!

```bash
# Execute a single command - super simple!
npm run start -- -d 313938 -c "help"

# Get device info
npm run start -- -d 313938 -c "device id"

# Check network status
npm run start -- -d 313938 -c "net iface 1"

# MQTT status
npm run start -- -d 313938 -c "mqtt status"

# LED control
npm run start -- -d 313938 -c "app led on 0"
npm run start -- -d 313938 -c "app led off 0"

# Battery status
npm run start -- -d 313938 -c "app battery"

# Capture output to a file
npm run start -- -d 313938 -c "help" > device_help.txt

# Use in scripts
DEVICE_ID=313938
MQTT_STATUS=$(npm run start -- -d $DEVICE_ID -c "mqtt status" 2>/dev/null)
echo "$MQTT_STATUS"
```

## Available Commands

In interactive mode, type `help` to see all available commands:

- **led** - LED control (on/off/toggle)
- **battery** - Battery status
- **info** - Device information
- **mqtt** - MQTT client control
- **audio** - Audio player commands
- **rtc** - Real-time clock commands
- **net** - Network control (ethernet/wifi/modem)
- **watchdog** - Watchdog control
- **charger** - Charger control
- **dfu** - DFU management
- **poweroff** - System shutdown

## Interactive Mode Features

- **Tab Completion**: Press Tab to auto-complete commands
- **Command History**: Use ↑/↓ arrow keys to navigate history
- **Clear Screen**: Type `clear` or `cls`
- **View History**: Type `history`
- **Exit**: Type `exit`, `quit`, or press Ctrl+C

## Examples

### Interactive Session

```
$ npm run start -- -d 313938
Connecting to MQTT broker at mqtt://localhost:1883...
✓ Connected to MQTT broker
Command topic: 313938_rx
Response topic: 313938_tx
✓ Subscribed to 313938_tx

╔════════════════════════════════════════════════════════════╗
║          RapidReach MQTT Shell Terminal v2.0               ║
╚════════════════════════════════════════════════════════════╝

Connected to device: 313938
Type commands to send to device, "exit" to quit
Use ↑/↓ for command history

mqtt-313938:~$ help
Available commands:
  clear        : Clear screen.
  date         : Date commands
  device       : Device commands
  devmem       : Read/write physical memory
  fs           : File system commands
  help         : Prints the help message.
  history      : Command history.
  kernel       : Kernel commands
  net          : Networking commands
  pm           : PM commands
  rapidreach   : RapidReach commands
  shell        : Useful, not Unix-like shell commands.
  wifi         : Wi-Fi commands

mqtt-313938:~$ device id
Device ID: 313938343233510e003d0029

mqtt-313938:~$ net iface 1
Interface eth0 (0x20008110) (Ethernet) [1]
==================================
Link addr : XX:XX:XX:XX:XX:XX
MTU       : 1500
Flags     : AUTO_START,IPv4,DHCP_OK
IPv4 unicast addresses (max 1):
    192.168.2.66/24 DHCP preferred

mqtt-313938:~$ exit

Disconnecting...
```

### Batch Commands

Create a script file `test-device.sh`:

```bash
#!/bin/bash
DEVICE_ID=313938

echo "=== Device Status Check ==="
npm run start -- -d $DEVICE_ID -c "device id"
npm run start -- -d $DEVICE_ID -c "mqtt status"
npm run start -- -d $DEVICE_ID -c "app battery"
npm run start -- -d $DEVICE_ID -c "net iface 1"

echo -e "\n=== LED Test ==="
npm run start -- -d $DEVICE_ID -c "app led on 0"
sleep 2
npm run start -- -d $DEVICE_ID -c "app led off 0"

echo -e "\n=== Test Complete ==="
```

Or use a simple alias for convenience:
```bash
alias mqtt-cmd='npm run start -- -d 313938 -c'

# Then use it like:
mqtt-cmd "help"
mqtt-cmd "mqtt status"
mqtt-cmd "app led on 0"
```

## Development

```bash
# Run in development mode (with hot reload)
npm run dev

# Clean build directory
npm run clean

# Build
npm run build
```

## Troubleshooting

1. **Connection Failed**: Check MQTT broker is running and accessible
2. **No Response**: Ensure device has MQTT CLI bridge enabled
3. **Timeout**: Increase timeout with `--timeout 10000`

## Requirements

- Node.js 18+ 
- MQTT broker (e.g., Mosquitto)
- RapidReach device with MQTT CLI bridge enabled

## License

MIT
