#!/bin/bash

# Generate test audio files for RapidReach testing
# Uses ffmpeg to create various test sounds

set -e

OUTPUT_DIR="${1:-.}"

echo "Generating test audio files in $OUTPUT_DIR..."

# Create directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

# 1. Simple beep/tone (1 second, 1000Hz)
echo "Creating beep.wav..."
ffmpeg -f lavfi -i "sine=frequency=1000:duration=1" \
    -ac 1 -ar 16000 \
    "$OUTPUT_DIR/beep.wav" -y 2>/dev/null

# 2. Alert sound (3 beeps)
echo "Creating alert.wav..."
ffmpeg -f lavfi -i "sine=frequency=800:duration=0.2" -af "apad=pad_dur=0.2" \
    -f lavfi -i "sine=frequency=800:duration=0.2" -af "adelay=400|400" \
    -f lavfi -i "sine=frequency=800:duration=0.2" -af "adelay=800|800" \
    -filter_complex "[0][1][2]amix=inputs=3" \
    -ac 1 -ar 16000 \
    "$OUTPUT_DIR/alert.wav" -y 2>/dev/null

# 3. Text-to-speech using espeak (if available)
if command -v espeak &> /dev/null; then
    echo "Creating speech alerts..."
    
    # Emergency alert
    espeak "Emergency alert. Please evacuate immediately." \
        -w "$OUTPUT_DIR/emergency.wav" \
        -s 150 -p 50 2>/dev/null
    
    # Test message
    espeak "This is a test of the RapidReach alert system." \
        -w "$OUTPUT_DIR/test_message.wav" \
        -s 160 -p 40 2>/dev/null
    
    # Device ready
    espeak "Device ready." \
        -w "$OUTPUT_DIR/device_ready.wav" \
        -s 180 -p 45 2>/dev/null
else
    echo "Note: espeak not installed, skipping speech synthesis"
    echo "Install with: sudo apt-get install espeak"
fi

# 4. Convert all to Opus format
echo ""
echo "Converting to Opus format..."
for wav in "$OUTPUT_DIR"/*.wav; do
    if [ -f "$wav" ]; then
        opus_file="${wav%.wav}.opus"
        ffmpeg -i "$wav" \
            -c:a libopus \
            -b:a 32k \
            -vbr on \
            -compression_level 10 \
            -application voip \
            -ac 1 \
            -ar 16000 \
            -f opus \
            "$opus_file" -y 2>/dev/null
        echo "  ✓ Created: $(basename "$opus_file") ($(du -h "$opus_file" | cut -f1))"
    fi
done

echo ""
echo "Test audio files generated successfully!"
echo ""
echo "To test with the API:"
echo "  curl -X POST http://localhost:3000/api/audio/alert \\"
echo "    -H 'Authorization: Bearer YOUR_TOKEN' \\"
echo "    -F 'audio=@$OUTPUT_DIR/alert.opus' \\"
echo "    -F 'deviceId=YOUR_DEVICE_ID' \\"
echo "    -F 'priority=10' \\"
echo "    -F 'volume=90'"
