# MQTT Shell Backend

The RapidReach device now supports a full duplex shell over MQTT, allowing remote shell access without a physical connection.

## Device Setup

1. Ensure MQTT is connected:
   ```
   rapidreach mqtt connect
   ```

2. Enable the MQTT shell backend:
   ```
   rapidreach mqtt shell enable
   ```

## Topics

- **Input**: `rapidreach/{device_id}/shell/in` - Send commands to the device
- **Output**: `rapidreach/{device_id}/shell/out` - Receive shell output from the device

## Using the MQTT Shell Terminal

### Interactive Terminal

Run the provided Node.js terminal application:

```bash
cd mqtt-terminal
npm run shell
```

This provides a full interactive shell experience over MQTT.

### Manual MQTT Commands

You can also use `mqttx` or any MQTT client:

1. Subscribe to output:
   ```bash
   mqttx sub -t "rapidreach/rapidreach_device/shell/out" -h 192.168.2.62 -p 1883
   ```

2. Send commands:
   ```bash
   mqttx pub -t "rapidreach/rapidreach_device/shell/in" -m "help\n" -h 192.168.2.62 -p 1883
   ```

Note: Commands must end with a newline character (`\n`).

## Features

- Full shell functionality including tab completion
- All shell commands work as if connected via serial
- Output is buffered and sent efficiently
- Multiple clients can subscribe to the output topic
- Low latency command execution

## Security Notes

- The MQTT shell provides full device access
- Ensure your MQTT broker has proper authentication
- Use TLS for production deployments
- Consider topic-based ACLs on your MQTT broker
