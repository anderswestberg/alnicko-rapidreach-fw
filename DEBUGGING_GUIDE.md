# Speaker Device Debugging Guide

## Quick Start

1. **First Time Setup**
   ```bash
   # Run the setup script
   ./setup-debugging.sh
   
   # Or manually:
   sudo apt install gdb-multiarch
   sudo ln -s /usr/bin/gdb-multiarch /usr/bin/arm-none-eabi-gdb
   ```

2. **Start Debugging in VSCode**
   - Press `F5` or click Run → Start Debugging
   - Select **"STlink launch (Simple)"** from dropdown

## Available Debug Configurations

| Configuration | Description | When to Use |
|--------------|-------------|-------------|
| **STlink launch (Simple)** | Direct debugging | Most common - attach to running device |
| **Program and Debug** | Flash + Debug | When you need to flash new firmware first |
| **STlink launch (External Server)** | External GDB server | Advanced debugging scenarios |

## Common Debugging Tasks

### Setting Breakpoints
- Click to the left of any line number
- Conditional breakpoints: Right-click → Add Conditional Breakpoint

### Viewing Variables
- **Variables Panel**: Shows all local variables
- **Watch Panel**: Add specific variables to monitor
- **Debug Console**: Type `p variable_name` to print any variable

### Thread Debugging
```gdb
info threads          # List all threads
thread 3             # Switch to thread 3
thread apply all bt  # Backtrace for all threads
```

### Memory Inspection
```gdb
x/10x 0x20000000     # Examine 10 hex words at address
info registers       # Show all registers
info stack          # Show stack
```

## Debugging Audio Issues

1. **Set breakpoints in key functions:**
   - `mqtt_audio_handler()` - When audio message arrives
   - `audio_player_set_volume()` - Volume changes
   - `audio_player_start()` - Audio playback starts

2. **Monitor key variables:**
   - `parsed_msg.metadata.volume` - Web volume (0-100)
   - `codec_volume` - Mapped codec value (-200 to +48)

3. **Check thread states:**
   ```gdb
   info threads
   # Look for:
   # - mqtt_thread
   # - audio_thread
   # - network threads
   ```

## Common Issues & Solutions

### "Cannot connect to target"
- Check ST-Link connection: `lsusb | grep ST-LINK`
- Reset the device
- Ensure no other debug session is running

### "No source available"
- Rebuild with debug symbols: `west build -b speaker`
- Check that source paths match

### Stack Overflow Detection
```gdb
# Check stack usage
info stack
# Look for stack canary values
x/x __stack_chk_guard
```

## Debug Output Locations

- **Serial Console**: Real-time logs via picocom
- **Debug Console**: GDB output and commands
- **Terminal**: Build and flash output

## Performance Profiling

1. **Check CPU usage:**
   ```gdb
   # Break after 10 seconds
   break k_sleep
   continue
   # Check thread CPU time
   info threads
   ```

2. **Memory usage:**
   ```gdb
   # Print heap statistics
   p sys_heap_runtime_stats_get
   ```

## Tips

- 🔴 Red dot = Active breakpoint
- 🟡 Yellow arrow = Current execution point
- Use Step Over (F10) for function calls you don't need to enter
- Use Step Into (F11) to debug inside functions
- Continue (F5) runs until next breakpoint

## Useful GDB Commands

```gdb
# Navigation
n           # Next line (step over)
s           # Step into function
c           # Continue execution
finish      # Run until current function returns

# Breakpoints
b main      # Break at main
b file.c:42 # Break at line 42 of file.c
info b      # List breakpoints
delete 1    # Delete breakpoint 1

# Information
bt          # Backtrace (call stack)
frame 2     # Switch to stack frame 2
list        # Show source code
```

## Debugging Workflow Example

```bash
# 1. Build with debug symbols
west build -b speaker

# 2. Start VSCode
code .

# 3. Open src/mqtt_module/mqtt_audio_handler.c

# 4. Set breakpoint at line 136 (volume mapping)

# 5. Press F5, select "STlink launch (Simple)"

# 6. Send audio alert from web interface

# 7. Debugger will stop at breakpoint

# 8. Inspect variables:
#    - parsed_msg.metadata.volume
#    - codec_volume

# 9. Step through code with F10
```

## Advanced: Debug Logging

Enable specific module debug output:
```bash
# Edit prj.conf or boards/speaker.conf
CONFIG_RPR_MODULE_MQTT_LOG_LEVEL=4      # Debug level
CONFIG_RPR_MODULE_AUDIO_LOG_LEVEL=4     # Debug level
```

Then rebuild and reflash.
