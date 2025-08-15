#!/bin/bash

# Gentle LED Disco - Slower pace to avoid overwhelming MQTT shell
DEVICE_ID=313938
HOST=192.168.2.62

# Send command with proper spacing
cmd() {
    npm run start -- -d $DEVICE_ID -h $HOST -c "$1" --timeout 1000 2>/dev/null
    sleep 0.3  # Give MQTT shell time to breathe between commands
}

# Turn all LEDs off
all_off() {
    echo "Turning all LEDs off..."
    cmd "app led off 0"
    cmd "app led off 1" 
    cmd "app led off 2"
    cmd "app led off 3"
}

# Cleanup on exit
cleanup() {
    echo -e "\n\n🛑 Stopping Gentle Disco..."
    all_off
    echo "Thanks for watching! 🎭"
    exit 0
}

trap cleanup SIGINT

clear
echo "╔═══════════════════════════════════════╗"
echo "║    🎵 GENTLE LED DISCO 🎵             ║"
echo "║     (MQTT-friendly version)           ║"
echo "║     Press Ctrl+C to stop              ║"
echo "╚═══════════════════════════════════════╝"
echo ""
echo "This version runs slower to keep MQTT connection stable"
echo ""

# Start with all off
all_off
sleep 2

while true; do
    # Wave pattern - one LED at a time
    echo "🌊 Gentle wave..."
    cmd "app led on 0"
    sleep 0.5
    cmd "app led off 0"
    cmd "app led on 1"
    sleep 0.5
    cmd "app led off 1"
    cmd "app led on 2"
    sleep 0.5
    cmd "app led off 2"
    cmd "app led on 3"
    sleep 0.5
    cmd "app led off 3"
    sleep 1
    
    # Double flash
    echo "💫 Double flash..."
    cmd "app led on 0"
    cmd "app led on 1"
    cmd "app led on 2"
    cmd "app led on 3"
    sleep 0.5
    all_off
    sleep 0.5
    cmd "app led on 0"
    cmd "app led on 1"
    cmd "app led on 2"
    cmd "app led on 3"
    sleep 0.5
    all_off
    sleep 1
    
    # Alternating pairs
    echo "🔄 Alternating pairs..."
    for i in {1..3}; do
        cmd "app led on 0"
        cmd "app led on 2"
        sleep 0.8
        cmd "app led off 0"
        cmd "app led off 2"
        sleep 0.3
        cmd "app led on 1"
        cmd "app led on 3"
        sleep 0.8
        cmd "app led off 1"
        cmd "app led off 3"
        sleep 0.3
    done
    
    # Chase pattern
    echo "🏃 Chase pattern..."
    for j in {1..2}; do
        for i in 0 1 2 3; do
            cmd "app led on $i"
            sleep 0.4
            cmd "app led off $i"
        done
    done
    
    # Build up and flash
    echo "📈 Build and flash..."
    cmd "app led on 0"
    sleep 0.5
    cmd "app led on 1"
    sleep 0.5
    cmd "app led on 2"
    sleep 0.5
    cmd "app led on 3"
    sleep 1
    all_off
    sleep 0.5
    
    echo -e "\n✨ Cycle complete! Starting next...\n"
    sleep 2
done
