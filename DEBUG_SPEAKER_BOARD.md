# Debugging the Speaker Board (STM32H573I-DK)

## 1. Quick Start - Using `west debug`

The easiest way to debug is using Zephyr's built-in debugging:

```bash
# Build with debug symbols
west build -b speaker -- -DCONFIG_DEBUG_OPTIMIZATIONS=y -DCONFIG_DEBUG_THREAD_INFO=y

# Start debugging
west debug
```

This will launch GDB and connect to your board via ST-LINK.

## 2. Setting Breakpoints for MQTT Issues

Once in GDB, set these key breakpoints:

```gdb
# Break at main entry
break main

# Break at domain logic start
break domain_logic_func

# Break at state machine transitions
break state_transition
break state_device_reg_complete_entry
break state_mqtt_init_start_entry

# Break at MQTT initialization
break mqtt_init
break mqtt_module_connect

# Break at network events
break net_mgmt_event_handler

# Run the program
continue
```

## 3. Step-by-Step Debugging Commands

```gdb
# Start from the beginning
monitor reset halt
continue

# When at a breakpoint:
next          # Step over
step          # Step into
finish        # Step out
info locals   # Show local variables
print ctx     # Print specific variable
backtrace     # Show call stack

# Continue to next breakpoint
continue
```

## 4. Watch for Key Variables

```gdb
# Watch state machine state
watch ctx->current_state

# Watch MQTT connection status
watch ctx->mqtt_connected

# Watch network status
watch net_ctx.connected
```

## 5. Debug Output via Serial

If GDB is too heavy, add debug prints:

```c
// In state_device_reg_complete_entry
LOG_ERR("DEBUG: DEVICE_REG_COMPLETE entry, transitioning to MQTT");

// In state_mqtt_init_start_entry
LOG_ERR("DEBUG: MQTT_INIT_START entry");

// In domain_logic network handler
LOG_ERR("DEBUG: Network event %d, connected=%d", mgmt_event, net_ctx.connected);
```

## 6. Alternative: Using OpenOCD Directly

```bash
# Terminal 1: Start OpenOCD
openocd -f board/stm32h573i-dk.cfg

# Terminal 2: Connect GDB
arm-none-eabi-gdb build/zephyr/zephyr.elf
(gdb) target remote localhost:3333
(gdb) monitor reset halt
(gdb) load
(gdb) continue
```

## 7. Common Issues to Check

1. **State Machine Stuck**: Set breakpoint at `retry_work_handler` to see if retry timer fires
2. **RTC Blocking**: Break at the RTC validation code in `domain_logic.c` line ~320
3. **Network Not Ready**: Break at `NET_EVENT_L4_CONNECTED` handler
4. **MQTT Init Skipped**: Check if `CONFIG_RPR_MODULE_MQTT` is defined

## 8. Quick Debug Session

```bash
# Build with debug
west build -b speaker -- -DCONFIG_DEBUG_OPTIMIZATIONS=y

# Flash
west flash

# Debug
west debug

# In GDB:
(gdb) break state_transition
(gdb) run
# Watch state transitions...
```
