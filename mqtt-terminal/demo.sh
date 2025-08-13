#!/bin/bash

# MQTT Terminal Demo Script
# This script demonstrates various commands

echo "=== RapidReach MQTT Terminal Demo ==="
echo

echo "1. Device Information:"
node dist/index.js -c "rapidreach info"
echo

sleep 1

echo "2. Battery Status:"
node dist/index.js -c "rapidreach battery"
echo

sleep 1

echo "3. MQTT Status:"
node dist/index.js -c "rapidreach mqtt status"
echo

sleep 1

echo "4. LED Control - Turn on green LED:"
node dist/index.js -c "rapidreach led on green"
echo

sleep 2

echo "5. LED Control - Turn off LED:"
node dist/index.js -c "rapidreach led off"
echo

sleep 1

echo "6. Test Command:"
node dist/index.js -c "rapidreach test"
echo

echo "=== Demo Complete ==="
echo "To run interactive mode, use: npm start"
