# Modem Buffer Optimization for Large File Transfers

## Issue Summary
When transferring large files (>60KB) over MQTT, the system experiences modem buffer overruns causing:
- "Receive buffer overrun" errors
- "Frame FCS error" messages 
- Dropped CMUX frames
- Failed file transfers

## Root Cause
The modem CMUX receive buffer (2048 bytes) was too small for incoming frames (up to 9327 bytes observed).

## Solution Applied
Increased buffer sizes in `boards/speaker.conf`:
```
CONFIG_MODEM_CELLULAR_CMUX_MAX_FRAME_SIZE=16384  # Was 2048
CONFIG_MODEM_CELLULAR_UART_BUFFER_SIZES=4096     # Was 512 (default)
```

## Additional Optimizations to Consider

### 1. Reduce File Write Blocking Time
The current implementation reads and writes data in 256-byte chunks, which may be too slow:
```c
// Current: 256-byte chunks
uint8_t chunk_buf[256];

// Consider: Larger chunks for better throughput
uint8_t chunk_buf[2048];
```

### 2. Increase Write Buffer Size
Larger write operations reduce syscall overhead:
```c
// Write in larger blocks to reduce filesystem overhead
#define WRITE_CHUNK_SIZE 4096
```

### 3. Add Flow Control
Implement backpressure to prevent overwhelming the modem:
```c
// Yield more frequently during large transfers
if (bytes_written % 2048 == 0) {
    k_yield();
    k_sleep(K_MSEC(1)); // Brief pause for modem to catch up
}
```

### 4. Monitor Buffer Usage
Add diagnostics to track buffer health:
```c
LOG_INF("Modem RX buffer usage: %d/%d", used, total);
```

## Testing
After rebuilding with these changes:
1. Flash the firmware: `./build-and-flash.sh`
2. Test with large file transfers (>60KB)
3. Monitor for buffer overrun errors
4. Verify complete file reception

## Long-term Improvements
1. Implement streaming file writes to avoid storing entire file in memory
2. Add modem flow control negotiation
3. Consider using DMA for UART transfers if supported
4. Implement adaptive chunk sizing based on modem load
