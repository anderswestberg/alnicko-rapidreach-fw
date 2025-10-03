#!/bin/bash
set -e

K3S_HOST="192.168.2.79"
K3S_USER="${1:-rapidreach}"

echo "Transferring images to k3s at ${K3S_HOST}..."
scp /tmp/rapidreach-images.tar.gz ${K3S_USER}@${K3S_HOST}:~/

echo ""
echo "Importing images on k3s host..."
ssh ${K3S_USER}@${K3S_HOST} "sudo k3s ctr images import ~/rapidreach-images.tar.gz && rm ~/rapidreach-images.tar.gz"

echo ""
echo "Cleaning up local tar file..."
rm /tmp/rapidreach-images.tar.gz

echo ""
echo "✅ Images imported successfully!"
echo ""
echo "Now restarting deployments..."

