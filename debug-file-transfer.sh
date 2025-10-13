#!/bin/bash
# Debug commands to run on the embedded device

echo "=== Checking filesystem status ==="
echo "fs stat /lfs"

echo -e "\n=== Checking for partial files ==="
echo "fs ls /lfs"

echo -e "\n=== Checking free space ==="
echo "fs statvfs /lfs"

echo -e "\n=== Checking device uptime ==="
echo "kernel uptime"

echo -e "\n=== Checking thread status ==="
echo "kernel threads"

echo -e "\n=== Checking for any error logs ==="
echo "log backend uart"
