#!/bin/bash

# Setup script for source code debugging of speaker device

echo "🔧 Setting up debugging environment for speaker device..."

# Check if running with sudo when needed
check_sudo() {
    if [ "$EUID" -ne 0 ]; then 
        echo "❌ This step requires sudo privileges. Please run: sudo $0"
        exit 1
    fi
}

# Step 1: Install gdb-multiarch
echo ""
echo "📦 Step 1: Checking gdb-multiarch..."
if ! command -v gdb-multiarch &> /dev/null; then
    echo "Installing gdb-multiarch..."
    if [ "$EUID" -eq 0 ]; then
        apt update && apt install -y gdb-multiarch
    else
        echo "❌ gdb-multiarch not found. Please run:"
        echo "   sudo apt update && sudo apt install gdb-multiarch"
        exit 1
    fi
else
    echo "✅ gdb-multiarch is already installed"
fi

# Step 2: Create symbolic link
echo ""
echo "🔗 Step 2: Creating symbolic link for arm-none-eabi-gdb..."
if [ ! -L /usr/bin/arm-none-eabi-gdb ]; then
    if [ "$EUID" -eq 0 ]; then
        ln -s /usr/bin/gdb-multiarch /usr/bin/arm-none-eabi-gdb
        echo "✅ Symbolic link created"
    else
        echo "❌ Symbolic link missing. Please run:"
        echo "   sudo ln -s /usr/bin/gdb-multiarch /usr/bin/arm-none-eabi-gdb"
        exit 1
    fi
else
    echo "✅ Symbolic link already exists"
fi

# Step 3: Check VSCode
echo ""
echo "📝 Step 3: VSCode setup..."
if command -v code &> /dev/null; then
    echo "✅ VSCode is installed"
    echo ""
    echo "⚠️  Please ensure you have installed the 'Cortex-Debug' extension:"
    echo "   1. Open VSCode"
    echo "   2. Press Ctrl+Shift+X"
    echo "   3. Search for 'Cortex-Debug' by marus25"
    echo "   4. Click Install"
else
    echo "❌ VSCode not found. Please install VSCode first."
fi

# Step 4: Verify ST-Link
echo ""
echo "🔌 Step 4: Checking ST-Link connection..."
if lsusb | grep -qi "STMicroelectronics.*STLINK"; then
    echo "✅ ST-Link detected"
else
    echo "⚠️  ST-Link not detected. Please connect your debugger."
fi

# Step 5: Build status
echo ""
echo "🔨 Step 5: Build status..."
if [ -f build/zephyr/zephyr.elf ]; then
    echo "✅ Firmware ELF file found"
    echo "   Debug symbols: $(file build/zephyr/zephyr.elf | grep -q "not stripped" && echo "✅ Present" || echo "❌ Missing")"
else
    echo "❌ No firmware found. Please build first: west build -b speaker"
fi

echo ""
echo "🎯 Setup complete! To start debugging:"
echo "   1. Open VSCode in this directory"
echo "   2. Open a source file (e.g., src/main.c)"
echo "   3. Set breakpoints by clicking line numbers"
echo "   4. Press F5 and select 'STlink launch (Simple)'"
echo ""
echo "💡 Tip: Use 'Program and Debug' to flash and debug in one step!"
