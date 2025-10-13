# Filesystem Hang Analysis

## Symptoms
- File transfer hangs after ~55KB written (chunk 106 of ~120)
- Last log: "Read 512 bytes, writing to file..."
- System becomes unresponsive but eventually recovers
- UART prompt appears after long delay

## Likely Causes

### 1. Filesystem Full or Near Full
LittleFS can become very slow when:
- Free space is low
- Garbage collection is needed
- Wear leveling is triggered

### 2. Flash Write Amplification
Writing 61KB in 512-1024 byte chunks creates many small writes, which can trigger:
- Block erasures (slow operation)
- Metadata updates
- Wear leveling operations

### 3. LittleFS Internal Operations
After ~50KB of writes, LittleFS might:
- Compact metadata
- Move blocks for wear leveling
- Perform garbage collection

## Debug Commands to Run

```bash
# Check filesystem usage
fs statvfs /lfs

# List files to see if old files exist
fs ls /lfs

# Check file sizes
fs ls /lfs -l

# Delete old audio files if needed
fs rm /lfs/mqtt_audio_*
fs rm /lfs/temp_*
```

## Potential Solutions

### 1. Pre-allocate File Space
Use larger write chunks to reduce metadata overhead

### 2. Clean Up Old Files
Delete temporary files before writing new ones

### 3. Increase Filesystem Size
Check partition size in device tree

### 4. Add Filesystem Maintenance
Periodically clean up old files
