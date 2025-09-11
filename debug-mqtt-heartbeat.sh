#!/bin/bash

echo "=== MQTT Heartbeat Debug ==="
echo ""
echo "1. Checking MQTT connections..."
kubectl exec -n rapidreach emqx-5b5b5667bd-8b5jb -- emqx ctl clients list 2>/dev/null | grep 313938 || echo "No 313938 clients found"

echo ""
echo "2. Checking MQTT subscriptions..."
kubectl exec -n rapidreach emqx-5b5b5667bd-8b5jb -- emqx ctl topics list 2>/dev/null | grep -E "(heartbeat|313938)"

echo ""
echo "3. Device server subscriptions..."
kubectl logs -n rapidreach deployment/device-server --tail=100 | grep -E "(Subscribe|heartbeat)" | tail -5

echo ""
echo "4. Recent device activity..."
kubectl logs -n rapidreach deployment/device-server --since=5m | grep -E "(313938|heartbeat|Device connected)" | tail -10

echo ""
echo "5. Starting real-time monitor for heartbeats..."
echo "   Heartbeats should appear every 30 seconds on: rapidreach/heartbeat/313938"
echo "   Press Ctrl+C to stop"
echo ""

kubectl logs -n rapidreach deployment/device-server -f | grep -E "(heartbeat|313938|Device)"
