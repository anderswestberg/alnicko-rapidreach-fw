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

### Interactive Mode

```bash
# Run with default settings
npm start

# Or run directly
node dist/index.js

# With custom broker
node dist/index.js -h 192.168.1.100 -p 1883
```

### Single Command Mode

```bash
# Execute a single command
node dist/index.js -c "rapidreach info"

# Turn on LED
node dist/index.js -c "rapidreach led on green"

# Check battery status
node dist/index.js -c "rapidreach battery"
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
$ npm start
Connecting to MQTT broker at mqtt://192.168.2.62:1883...
✓ Connected to MQTT broker
✓ Subscribed to rapidreach/rapidreach_device/cli/response

╔════════════════════════════════════════════════════════════╗
║          RapidReach MQTT Terminal v1.0.0                   ║
╚════════════════════════════════════════════════════════════╝

Type "help" for available commands, "exit" to quit
Use Tab for command completion, ↑/↓ for history

rapidreach> rapidreach info
→ Sending: rapidreach info
← Firmware version: V0.0.8
← Hardware version: R01
← Board name: Speaker Board
← Device ID: 313938343233510e003d0029

rapidreach> rapidreach led on green
→ Sending: rapidreach led on green
← OK

rapidreach> exit
Disconnecting...
```

### Batch Commands

Create a script file `commands.sh`:

```bash
#!/bin/bash
mqtt-terminal -c "rapidreach mqtt status"
mqtt-terminal -c "rapidreach battery"
mqtt-terminal -c "rapidreach led on red"
sleep 2
mqtt-terminal -c "rapidreach led off"
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
