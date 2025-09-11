#!/bin/bash
# Send initial command to device shell to prevent 90-second timeout

DEVICE_ID="313938"

echo "Sending initial shell command to device $DEVICE_ID..."

# Use kubectl exec to send MQTT message
kubectl exec -n rapidreach deployment/emqx -- \
  mosquitto_pub -h localhost -p 1883 -u admin -P public \
  -t "rapidreach/$DEVICE_ID/shell/in" -m ""

echo "Initial command sent. Device should now stay connected."
