# MQTT CLI Bridge Usage Guide

## Overview

The MQTT CLI Bridge allows remote command execution on the RapidReach device via MQTT. This enables:
- Remote device management without physical access
- Automated testing via MQTT
- AI tools interaction through mqttx
- Multiple simultaneous CLI sessions

## How to Use

### 1. Enable MQTT CLI Bridge on Device

First, ensure MQTT is initialized and connected:
```bash
rapidreach mqtt init
rapidreach mqtt connect
rapidreach mqtt status
```

Then enable the CLI bridge:
```bash
rapidreach mqtt cli enable
```

This will display:
- Command topic: `rapidreach/{device_id}/cli/command`
- Response topic: `rapidreach/{device_id}/cli/response`

### 2. Send Commands via MQTT

From a remote system using mqttx:

```bash
# Subscribe to response topic to see command output
mqttx sub -t "rapidreach/rapidreach_device/cli/response" -h 192.168.2.62 -p 1883

# In another terminal, send commands
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach led on green" -h 192.168.2.62 -p 1883
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach battery" -h 192.168.2.62 -p 1883
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach device info" -h 192.168.2.62 -p 1883
```

### 3. Command Examples

```bash
# LED Control
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach led on green"
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach led off"

# Audio Control
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach audio volume get"
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach audio volume set 50"
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach audio play 0"

# System Information
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach battery"
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach device info"
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach rtc get"

# Network Status
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach net modem status"
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach mqtt status"
```

### 4. Disable MQTT CLI Bridge

To disable the bridge:
```bash
rapidreach mqtt cli disable
```

Or via MQTT:
```bash
mqttx pub -t "rapidreach/rapidreach_device/cli/command" -m "rapidreach mqtt cli disable"
```

## Security Considerations

1. The MQTT CLI bridge provides full access to the device's CLI
2. Ensure MQTT broker has proper authentication
3. Use TLS for MQTT connections in production
4. Consider implementing command filtering/whitelisting

## Implementation Notes

- Commands are executed asynchronously in a dedicated work queue
- Response buffer is limited to 1024 bytes
- Both USB serial and MQTT CLI work simultaneously
- The implementation captures shell output and sends it via MQTT

## Troubleshooting

1. **No response received**: Check MQTT connection status
2. **Commands not executed**: Verify CLI bridge is enabled
3. **Partial output**: Some commands may produce output larger than buffer size
4. **MQTT not connected**: Ensure device has network connectivity

## Future Enhancements

- Command history/session management
- Authentication for CLI commands
- Output streaming for large responses
- Command timeout handling
- Rate limiting for security
