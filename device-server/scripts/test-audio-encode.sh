#!/bin/bash

# Test script for audio encoding to Opus format
# Usage: ./test-audio-encode.sh <input-file> [output-file]

set -e

INPUT_FILE="$1"
OUTPUT_FILE="${2:-output.opus}"

if [ -z "$INPUT_FILE" ]; then
    echo "Usage: $0 <input-file> [output-file]"
    echo "Converts audio file to Opus format suitable for RapidReach devices"
    exit 1
fi

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file '$INPUT_FILE' not found"
    exit 1
fi

# Check if ffmpeg is installed
if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed"
    echo "Install with: sudo apt-get install ffmpeg"
    exit 1
fi

echo "Converting '$INPUT_FILE' to Opus format..."

# Convert to Opus with settings optimized for speech/alerts
# -c:a libopus - Use Opus codec
# -b:a 32k - 32kbps bitrate (good for speech)
# -vbr on - Variable bitrate for better quality
# -compression_level 10 - Maximum compression
# -application voip - Optimize for speech
# -ac 1 - Convert to mono
# -ar 16000 - 16kHz sample rate
ffmpeg -i "$INPUT_FILE" \
    -c:a libopus \
    -b:a 32k \
    -vbr on \
    -compression_level 10 \
    -application voip \
    -ac 1 \
    -ar 16000 \
    -f opus \
    "$OUTPUT_FILE" \
    -y

if [ $? -eq 0 ]; then
    echo "✓ Successfully converted to: $OUTPUT_FILE"
    echo "  File size: $(du -h "$OUTPUT_FILE" | cut -f1)"
    
    # Show how to create MQTT message
    echo ""
    echo "To send via MQTT, create a message with:"
    echo "1. JSON header: {\"opus_data_size\":$(stat -c%s "$OUTPUT_FILE"),\"priority\":10,\"volume\":90}"
    echo "2. Followed by the binary content of $OUTPUT_FILE"
else
    echo "✗ Conversion failed"
    exit 1
fi
