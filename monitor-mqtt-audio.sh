#!/bin/bash

echo "🎵 Monitoring MQTT Audio Messages"
echo "================================="
echo ""
echo "Subscribing to audio topics to see what's being sent..."
echo ""

# Monitor the audio topic
kubectl exec -n rapidreach emqx-5b5b5667bd-8b5jb -- emqx ctl trace start rapidreach/audio/+ all 2>/dev/null

echo "Monitoring started. Send an audio alert to see the MQTT traffic."
echo "Press Ctrl+C to stop"
echo ""

# Show trace logs
kubectl exec -n rapidreach emqx-5b5b5667bd-8b5jb -f -- tail -f /opt/emqx/log/trace*.log 2>/dev/null
