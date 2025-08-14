# MQTT Client ID Guide for RapidReach

## Understanding MQTT Client IDs

The RapidReach firmware uses two separate MQTT connections:

1. **Main MQTT Module** - For application-level communication (heartbeat, data, etc.)
2. **MQTT Shell Backend** - For remote CLI access

Each uses a different client ID format.

## MQTT Shell Backend Client ID

The Zephyr MQTT shell backend automatically generates its client ID using the **first 6 characters of the device ID**.

### Example:
- Full Device ID: `313938343233510e003d0029`
- MQTT Shell Client ID: `313938`

This is what appears in the EMQX dashboard client list.

### Why Only 6 Characters?

The MQTT shell backend uses a shortened ID to:
- Keep topic names short (topics are `{client_id}_rx` and `{client_id}_tx`)
- Ensure compatibility across different MQTT brokers
- Make it easier to type when using manual MQTT tools

## Main MQTT Module Client ID

The main MQTT module uses the configured client ID from `CONFIG_RPR_MQTT_CLIENT_ID`, which defaults to "rapidreach_device".

You can customize this in your board configuration:
```conf
CONFIG_RPR_MQTT_CLIENT_ID="rrsa-device-001"
```

## Identifying Devices in EMQX

When looking at the EMQX dashboard:

1. **Shell Connections**: Look for 6-character hex IDs (e.g., `313938`)
2. **Main Module Connections**: Look for "rapidreach_device" or your custom ID

### Recommended Naming Convention

For production deployments, consider:
- Main Module: `rrsa-{location}-{number}` (e.g., `rrsa-building1-001`)
- Shell Backend: Automatic (first 6 chars of device ID)

## Finding Your Device

1. **EMQX Dashboard Method**:
   - Navigate to http://localhost:18083
   - Login with admin/public
   - Go to "Clients" tab
   - Look for your 6-character device ID

2. **From Device Logs**:
   ```
   [00:14:50.469,000] <inf> shell_mqtt: Logs will be published to: 313938_tx
   [00:14:50.469,000] <inf> shell_mqtt: Subscribing shell cmds from: 313938_rx
   ```

3. **Using mqtt-terminal**:
   ```bash
   # Just use the 6-character prefix
   npm run start -- -d 313938
   ```

## Topic Structure

The MQTT shell backend uses simple topic names:
- Commands TO device: `{client_id}_rx`
- Responses FROM device: `{client_id}_tx`

Example:
- Device ID starts with: `313938`
- Command topic: `313938_rx`
- Response topic: `313938_tx`

This makes it easy to interact with devices using standard MQTT tools.
