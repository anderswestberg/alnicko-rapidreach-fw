# This script processes all board builds,
# checks for the presence of signed firmware binaries,
# renames them according to the version, 
# and copies them to the firmware_updates folder.

import os
import shutil
from pathlib import Path

script_dir = Path(__file__).resolve().parent
project_root = script_dir.parent
build_dir = project_root / "build"
version_file = project_root / "VERSION"

if not build_dir.exists():
    print("❌ The build directory was not found.")
    exit(1)

boards = [d for d in build_dir.iterdir() if d.is_dir()]
if not boards:
    print("❌ No board builds found in the build directory.")
    exit(1)

if not version_file.exists():
    print("❌ VERSION file not found.")
    exit(1)

version_vars = {}
with open(version_file) as vf:
    for line in vf:
        if "=" in line:
            key, val = line.strip().split("=")
            version_vars[key.strip()] = val.strip()

version = "V{VERSION_MAJOR}.{VERSION_MINOR}.{PATCHLEVEL}".format(**version_vars)

output_dir = project_root / "firmware_updates"
output_dir.mkdir(exist_ok=True)

prepared_binaries = []

for board_dir in boards:
    board_name = board_dir.name
    bin_path = None

    rapid_fw_zephyr = board_dir / "rapidreach-fw" / "zephyr" / "zephyr.signed.bin"
    zephyr_bin = board_dir / "zephyr" / "zephyr.signed.bin"

    if rapid_fw_zephyr.exists():
        bin_path = rapid_fw_zephyr
    elif zephyr_bin.exists():
        print(f"⚠️ 'rapidreach-fw' folder not found in {board_name}, falling back to 'zephyr'")
        bin_path = zephyr_bin
    else:
        print(f"❌ zephyr.signed.bin not found for board '{board_name}'.")
        continue

    target_filename = f"{board_name}_rapidreach-fw_{version}.bin"
    target_path = output_dir / target_filename
    shutil.copy2(bin_path, target_path)
    prepared_binaries.append(target_path)

if prepared_binaries:
    print("\n✅ Firmware update files prepared successfully. They are located in:")
    print(f"{output_dir}\n")
    for path in prepared_binaries:
        print(f"➡️  {path.name}\n")
else:
    print("❌ No firmware binaries were prepared.")
