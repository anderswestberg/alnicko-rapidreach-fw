#!/bin/bash

# Simple debug setup script for STM32H573 via ST-LINK V3
# This script programs the firmware and starts a GDB server

STLINK_SERIAL="004B001F3234510936303532"
CUBE_PROG="/opt/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.200.202503041107/tools/bin/STM32_Programmer_CLI"
STLINK_GDB="/opt/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.linux64_2.2.200.202505060755/tools/bin/ST-LINK_gdbserver"

echo "=== STM32H573 Debug Setup ==="

# Step 1: Reset and connect to target
echo "Step 1: Connecting to target..."
$CUBE_PROG -c port=SWD sn=$STLINK_SERIAL mode=HOTPLUG -hardRst
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to connect to target"
    exit 1
fi

# Step 2: Program firmware
echo "Step 2: Programming firmware..."
$CUBE_PROG -c port=SWD sn=$STLINK_SERIAL mode=HOTPLUG -w build/zephyr/zephyr.hex -v -rst
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to program firmware"
    exit 1
fi

echo "Step 3: Starting GDB server on port 61234..."
echo "You can now connect with GDB using: target remote localhost:61234"

# Step 3: Start GDB server in attach mode
$STLINK_GDB -p 61234 -l 1 -d -s -cp "/opt/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.200.202503041107/tools/bin" -m 0 --swd -i $STLINK_SERIAL -g
