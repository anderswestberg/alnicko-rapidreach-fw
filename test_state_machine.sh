#!/bin/bash
# Test script for the startup state machine

echo "Testing Startup State Machine"
echo "============================="
echo ""
echo "This script will help you test the startup state machine."
echo ""

# Check if the state machine is enabled
if grep -q "CONFIG_RPR_MODULE_INIT_SM=y" prj.conf; then
    echo "✓ State machine is ENABLED in prj.conf"
else
    echo "✗ State machine is DISABLED in prj.conf"
    echo "  Add 'CONFIG_RPR_MODULE_INIT_SM=y' to prj.conf to enable"
    exit 1
fi

echo ""
echo "To test the state machine:"
echo "1. Build and flash the firmware: west build -p && west flash"
echo "2. Connect to the device console to see the state transitions"
echo "3. Look for these key log messages:"
echo "   - 'Starting device initialization state machine'"
echo "   - 'Entering WAIT_NETWORK state'"
echo "   - 'Entering NETWORK_READY state'"
echo "   - 'Entering DEVICE_REG_START state' (if device registry enabled)"
echo "   - 'Entering MQTT_INIT_START state'"
echo "   - 'Entering MQTT_CONNECTING state'"
echo "   - 'Entering OPERATIONAL state - system fully initialized!'"
echo ""
echo "4. Check for error handling:"
echo "   - Disconnect network to see state machine handle network loss"
echo "   - Stop MQTT broker to see reconnection attempts"
echo ""
echo "5. Monitor state transitions with: grep 'init_sm' in the logs"
