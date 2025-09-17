#!/bin/bash
# Update k3s deployments with new Docker Hub images

K3S_HOST="${1:-192.168.2.79}"
K3S_USER="${2:-rapidreach}"
VERSION="${3:-v1.0.25}"

echo "Updating RapidReach deployments on k3s..."
echo "Host: $K3S_HOST"
echo "User: $K3S_USER"
echo "Version: $VERSION"

# Create update commands
cat > /tmp/k3s-update-commands.sh << 'EOF'
#!/bin/bash
echo "Updating device-server to version: $1"
sudo kubectl set image deployment/device-server device-server=enerahub/device-server:$1 -n rapidreach

echo "Updating web-app to version: $1"
sudo kubectl set image deployment/web-app web-app=enerahub/web-app:$1 -n rapidreach

echo ""
echo "Checking rollout status..."
sudo kubectl rollout status deployment/device-server -n rapidreach --timeout=60s || true
sudo kubectl rollout status deployment/web-app -n rapidreach --timeout=60s || true

echo ""
echo "Current deployments:"
sudo kubectl get deployments -n rapidreach
sudo kubectl get pods -n rapidreach
EOF

chmod +x /tmp/k3s-update-commands.sh

echo ""
echo "Copying update script to k3s host..."
scp /tmp/k3s-update-commands.sh ${K3S_USER}@${K3S_HOST}:~/

echo ""
echo "Executing update on k3s host..."
ssh ${K3S_USER}@${K3S_HOST} "bash ~/k3s-update-commands.sh $VERSION"

# Cleanup
rm /tmp/k3s-update-commands.sh
ssh ${K3S_USER}@${K3S_HOST} "rm ~/k3s-update-commands.sh"

echo ""
echo "✅ Deployment update completed!"
echo ""
echo "Access points:"
echo "- Web App: http://${K3S_HOST}:30080"
echo "- Device Server API: http://${K3S_HOST}:30002"
echo ""
echo "To check status:"
echo "  ssh ${K3S_USER}@${K3S_HOST}"
echo "  kubectl get pods -n rapidreach"
