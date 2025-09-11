# Firmware Update - MQTT Broker IP Change

## Summary
Updated MQTT broker IP from `192.168.2.62` to `192.168.2.79` to match the k3s deployment.

## Changed Files
- `prj.conf` - CONFIG_SHELL_MQTT_SERVER_ADDR
- `boards/speaker.conf` - CONFIG_RPR_MQTT_BROKER_HOST  
- `boards/stm32h573i_dk.conf` - CONFIG_RPR_MQTT_BROKER_HOST
- `src/examples/alnicko_server.c` - All server URLs
- `src/http_log_client/Kconfig` - Default log server URL
- `src/http_log_client/http_log_client.c` - Fallback IP

## Build Commands

### For Speaker Board:
```bash
# Clean build
west build -p -b speaker

# Flash device
west flash
```

### For STM32H573I-DK Board:
```bash
# Clean build
west build -p -b stm32h573i_dk

# Flash device
west flash
```

## Verify Connection
After flashing, the device should connect to:
- MQTT Broker: `192.168.2.79:1883`
- Username: `admin`
- Password: `public`

Monitor connections at: http://192.168.2.79:18083

