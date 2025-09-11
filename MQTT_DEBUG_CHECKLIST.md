# MQTT Connection Debug Checklist

## Problem: Main MQTT client not connecting despite shell client working

### 1. Check Device Serial Console
Connect to the device's serial console to see boot logs:
- Look for "Entering MQTT_INIT_START state"
- Look for "MQTT module initialized" 
- Look for any error messages

### 2. Verify Network Connectivity
Since shell client works, network is OK, but check:
- Device IP address (should be in 192.168.2.x range)
- Can device reach MQTT broker at 192.168.2.79:1883

### 3. Check EMQX Dashboard
Access at http://192.168.2.79:18083 (admin/public)
- Look for client ID: 313938-speaker
- Look for client ID: 313938-shell
- Check if there are connection attempts

### 4. Potential Root Causes
1. **MQTT module not initializing** - State machine might still be stuck
2. **RTC validation failing** - MQTT waits for valid RTC time
3. **Network event not firing** - Domain logic waits for network event
4. **MQTT credentials/config issue** - Wrong broker config

### 5. Quick Test Commands
```bash
# Monitor device server logs in real-time
kubectl logs -n rapidreach deployment/device-server -f | grep -E "(313938|heartbeat|Device)"

# Check all MQTT topics
kubectl exec -n rapidreach deployment/emqx-0 -- emqx_ctl topics list | grep 313938

# Check connected clients
kubectl exec -n rapidreach deployment/emqx-0 -- emqx_ctl clients list | grep 313938
```

### 6. If Nothing Works
The device firmware might need deeper debugging:
1. Enable more verbose logging
2. Add debug prints in init state machine transitions
3. Check if domain_logic_func is actually running
4. Verify MQTT broker host/port configuration
