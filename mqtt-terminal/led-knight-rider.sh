#!/bin/bash

# Knight Rider LED Scanner
# The iconic back-and-forth red light from KITT!

DEVICE_ID=313938

# LED control function
led() {
    npm run start -- -d $DEVICE_ID -c "app led $2 $1" 2>/dev/null >/dev/null
}

# Turn all LEDs off
all_off() {
    for i in 0 1 2 3; do
        led $i off
    done
}

# Cleanup on exit
cleanup() {
    echo -e "\n\n🚗 KITT is parking..."
    all_off
    echo "See you later, Michael! 🚗"
    exit 0
}

trap cleanup SIGINT

# Intro
echo "╔════════════════════════════════════════╗"
echo "║     🚗 KNIGHT RIDER LED SCANNER 🚗     ║"
echo "║        Press Ctrl+C to stop            ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "🚗 KITT: 'Hello Michael...'"
echo ""

# Initialize
all_off
sleep 1

# Main loop
while true; do
    # Forward scan
    for i in 0 1 2 3; do
        [ $i -gt 0 ] && led $((i-1)) off
        led $i on
        sleep 0.08
    done
    
    # Backward scan
    for i in 3 2 1 0; do
        [ $i -lt 3 ] && led $((i+1)) off
        led $i on
        sleep 0.08
    done
done
