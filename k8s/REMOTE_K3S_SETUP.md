# Connecting to Remote k3s from Dev Machine

## 1. Get the k3s kubeconfig from your k3s host

SSH to your k3s host and copy the kubeconfig:

```bash
# On k3s host
sudo cat /etc/rancher/k3s/k3s.yaml
```

## 2. Configure kubectl on your dev machine

```bash
# Create kubectl config directory if it doesn't exist
mkdir -p ~/.kube

# Create/edit the config file
nano ~/.kube/config

# Paste the content from k3s.yaml, but CHANGE the server URL:
# Change: server: https://127.0.0.1:6443
# To:     server: https://YOUR_K3S_HOST_IP:6443
```

## 3. Install kubectl on dev machine (if not already installed)

```bash
# For Debian/Ubuntu
curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
rm kubectl
```

## 4. Test the connection

```bash
kubectl get nodes
# Should show your k3s node
```

## 5. Deploy from your dev machine

```bash
cd /home/rapidreach/work/alnicko-rapidreach-fw/k8s
kubectl apply -k .
```

## Alternative: Quick Setup Script

Create this script on your dev machine:

```bash
#!/bin/bash
# setup-k3s-remote.sh

K3S_HOST="YOUR_K3S_HOST_IP"
K3S_USER="YOUR_K3S_USERNAME"

# Get kubeconfig from k3s host
echo "Getting kubeconfig from k3s host..."
ssh ${K3S_USER}@${K3S_HOST} "sudo cat /etc/rancher/k3s/k3s.yaml" > /tmp/k3s.yaml

# Update the server URL
sed -i "s/127.0.0.1/${K3S_HOST}/g" /tmp/k3s.yaml

# Backup existing config if it exists
[ -f ~/.kube/config ] && cp ~/.kube/config ~/.kube/config.backup

# Install the new config
mkdir -p ~/.kube
cp /tmp/k3s.yaml ~/.kube/config
chmod 600 ~/.kube/config

# Test
echo "Testing connection..."
kubectl get nodes
```

Make it executable and run:
```bash
chmod +x setup-k3s-remote.sh
./setup-k3s-remote.sh
```
