#!/bin/bash
# Setup kubectl to connect to remote k3s cluster

set -e

echo "=== RapidReach k3s Remote Access Setup ==="
echo

# Check if kubectl is installed
if ! command -v kubectl &> /dev/null; then
    echo "📦 Installing kubectl..."
    curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
    sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
    rm kubectl
    echo "✅ kubectl installed"
else
    echo "✅ kubectl already installed ($(kubectl version --client --short 2>/dev/null || echo 'version check failed'))"
fi

# Get k3s host details
echo
read -p "Enter your k3s host IP address: " K3S_HOST
read -p "Enter your k3s host username [default: $USER]: " K3S_USER
K3S_USER=${K3S_USER:-$USER}

# Fetch kubeconfig
echo
echo "🔐 Fetching kubeconfig from k3s host..."
echo "   (You may be prompted for your SSH password or key passphrase)"

TEMP_KUBECONFIG="/tmp/k3s-kubeconfig-$$"
if ssh ${K3S_USER}@${K3S_HOST} "sudo cat /etc/rancher/k3s/k3s.yaml" > "$TEMP_KUBECONFIG" 2>/dev/null; then
    echo "✅ Kubeconfig fetched successfully"
else
    echo "❌ Failed to fetch kubeconfig. Please ensure:"
    echo "   - SSH access to ${K3S_USER}@${K3S_HOST} is working"
    echo "   - k3s is installed and running on the host"
    echo "   - User ${K3S_USER} has sudo privileges"
    exit 1
fi

# Update server URL
sed -i "s/127.0.0.1/${K3S_HOST}/g" "$TEMP_KUBECONFIG"

# Backup existing config
if [ -f ~/.kube/config ]; then
    echo "📋 Backing up existing kubeconfig to ~/.kube/config.backup"
    cp ~/.kube/config ~/.kube/config.backup
fi

# Install new config
mkdir -p ~/.kube
cp "$TEMP_KUBECONFIG" ~/.kube/config
chmod 600 ~/.kube/config
rm "$TEMP_KUBECONFIG"

echo
echo "🔍 Testing connection..."
if kubectl get nodes; then
    echo
    echo "✅ Successfully connected to k3s cluster!"
    echo
    echo "📝 Next steps:"
    echo "   1. Deploy RapidReach:"
    echo "      kubectl apply -k ."
    echo
    echo "   2. Check deployment status:"
    echo "      kubectl get pods -n rapidreach"
    echo
    echo "   3. Get service endpoints:"
    echo "      kubectl get svc -n rapidreach"
else
    echo
    echo "❌ Connection test failed. Please check:"
    echo "   - k3s is running on ${K3S_HOST}"
    echo "   - Port 6443 is accessible from this machine"
    echo "   - No firewall blocking the connection"
fi
