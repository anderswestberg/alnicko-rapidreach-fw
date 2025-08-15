# MQTT Terminal Usage Guide

## Quick Start

The MQTT terminal provides remote CLI access to RapidReach devices via MQTT.

### Starting the Terminal

```bash
npm run start -- --host localhost --port 1883 --device <DEVICE_ID> --username admin --password public
```

### Important: Device ID Handling

The MQTT shell backend uses only the **first 6 characters** of the device ID for topic names:
- Command topic: `{device_id_prefix}_rx`
- Response topic: `{device_id_prefix}_tx`

For example, if your device ID is `313938343233510e003d0029`, the topics will be:
- `devices/313938/rx` (commands to device)
- `devices/313938/tx` (responses from device)

You can use any device ID that starts with the correct 6-character prefix.

### Available Commands

Once connected, you can use all the device's shell commands:

- `help` - Show all available commands
- `device id` - Display device ID
- `net iface 1` - Show network interface status
- `rapidreach` - RapidReach-specific commands
- `wifi` - WiFi management commands
- `fs` - File system commands
- And many more...

### Single Command Mode

Execute a single command and exit:

```bash
npm run start -- --host localhost --port 1883 --device 313938000000000000000000 --username admin --password public --command "device id"
```

### Environment Variables

You can set defaults using environment variables in `.env`:

```env
MQTT_BROKER_HOST=localhost
MQTT_BROKER_PORT=1883
MQTT_USERNAME=admin
MQTT_PASSWORD=public
DEVICE_ID=313938000000000000000000
```

### Logging

The MQTT terminal automatically logs to the centralized log server at `http://localhost:3001`. Logs include:
- Connection events
- Commands sent
- Responses received
- Errors and warnings

View logs:
```bash
curl http://localhost:3001/logs/mqtt-terminal
```

### Troubleshooting

1. **No response from device**: 
   - Ensure the device has network connectivity
   - Check if DHCP is enabled: `net dhcpv4 client start 1`
   - Verify MQTT connection in device logs

2. **Connection refused**:
   - Ensure EMQX is running: `cd ../log-server && ./manage.sh status`
   - Check credentials match EMQX configuration

3. **Wrong device ID**:
   - Look for "Device ID detected:" in device boot logs
   - Or use the serial console to run `device id`
   - Remember only the first 6 characters matter for MQTT topics
