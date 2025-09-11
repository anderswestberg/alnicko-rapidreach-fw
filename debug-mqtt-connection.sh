#!/bin/bash

echo "=== MQTT Connection Debug Script ==="
echo ""

# Check EMQX for connected clients
echo "1. Checking connected MQTT clients..."
echo "   EMQX Dashboard: http://192.168.2.79:18083 (admin/public)"
echo ""

# Check device server logs
echo "2. Recent device server logs:"
kubectl logs -n rapidreach deployment/device-server --tail=20 | grep -E "(heartbeat|313938|speaker|Device|message from|MQTT)" || echo "No relevant logs found"
echo ""

# Check if API is responding
echo "3. Device server API status:"
curl -s http://192.168.2.79:30002/api/devices 2>/dev/null | head -20 || echo "API endpoint not responding"
echo ""

# Monitoring instructions
echo "4. To monitor in real-time:"
echo "   kubectl logs -n rapidreach deployment/device-server -f | grep -E '(313938|heartbeat|Device)'"
echo ""

echo "5. Expected behavior after firmware flash:"
echo "   - Device should connect with ID: 313938-speaker"
echo "   - Heartbeats should appear every 30 seconds"
echo "   - Shell client (313938-shell) should also be connected"
echo ""

echo "6. If device is not connecting:"
echo "   a) Power cycle the device"
echo "   b) Check serial console for boot logs"
echo "   c) Verify network connectivity (device IP should be in 192.168.2.x range)"
