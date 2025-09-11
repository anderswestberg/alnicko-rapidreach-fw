#!/bin/bash
# Quick setup commands to run on your k3s host (192.168.2.62)

echo "=== K3s Post-Install Setup ==="

# 1. Check k3s is running
echo "Checking k3s status..."
sudo systemctl status k3s --no-pager

# 2. Wait for k3s to be ready
echo
echo "Waiting for k3s to be ready..."
sudo k3s kubectl wait --for=condition=Ready nodes --all --timeout=60s

# 3. Install MetalLB
echo
echo "Installing MetalLB..."
sudo k3s kubectl apply -f https://raw.githubusercontent.com/metallb/metallb/v0.13.7/config/manifests/metallb-native.yaml

# 4. Wait for MetalLB pods
echo
echo "Waiting for MetalLB to be ready..."
sudo k3s kubectl wait --for=condition=ready pod -l app=metallb -n metallb-system --timeout=90s

# 5. Show the kubeconfig (for remote access)
echo
echo "=== KUBECONFIG FOR REMOTE ACCESS ==="
echo "Copy this entire output to set up kubectl on your dev machine:"
echo "---START---"
sudo cat /etc/rancher/k3s/k3s.yaml
echo "---END---"

# 6. Optional: Make kubeconfig readable by your user (for local kubectl)
echo
read -p "Make kubeconfig readable by your user for local kubectl? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Copy to user's home
    mkdir -p ~/.kube
    sudo cp /etc/rancher/k3s/k3s.yaml ~/.kube/config
    sudo chown $(id -u):$(id -g) ~/.kube/config
    chmod 600 ~/.kube/config
    
    # Update the server address to use localhost
    sed -i 's/127.0.0.1/127.0.0.1/g' ~/.kube/config
    
    echo "✅ kubectl configured for local use"
    echo "Test with: kubectl get nodes"
fi

echo
echo "=== Next Steps ==="
echo "1. Copy the kubeconfig output above"
echo "2. Return to your dev machine" 
echo "3. Run: ./setup-kubectl.sh"
echo "4. When prompted, paste the kubeconfig"
