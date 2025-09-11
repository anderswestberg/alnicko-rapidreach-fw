#!/bin/bash

echo "🔧 Building firmware with device ID fix..."
echo ""
echo "The fix: When device registry is disabled, we now use"
echo "the first 6 characters of the hardware ID (313938) as the device ID."
echo ""

# Build
west build -b speaker

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Build successful! Flashing..."
    west flash
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "✅ Flash complete!"
        echo ""
        echo "Now reconnect to serial console:"
        echo "sudo picocom -b 115200 /dev/ttyACM0"
        echo ""
        echo "You should see:"
        echo "- 'Using fallback device ID: 313938'"
        echo "- 'Starting MQTT initialization with device ID: 313938'"
        echo "- MQTT connection success!"
    fi
else
    echo "❌ Build failed!"
fi
