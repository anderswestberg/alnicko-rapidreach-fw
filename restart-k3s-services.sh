#!/bin/bash

# Script to restart/scale up RapidReach services on k3s
set -e

K3S_HOST="${1:-192.168.2.79}"

echo "Restarting RapidReach services on ${K3S_HOST}..."

# Scale up the deployments
ssh ${K3S_HOST} << 'EOF'
echo "Scaling up device-server..."
kubectl scale deployment device-server --replicas=1 -n rapidreach

echo "Scaling up web-app..."
kubectl scale deployment web-app --replicas=1 -n rapidreach

echo ""
echo "Waiting for pods to be ready..."
kubectl wait --namespace rapidreach \
  --for=condition=available \
  --timeout=120s \
  deployment/device-server \
  deployment/web-app

echo ""
echo "✓ Services are running!"
echo ""
echo "Pod status:"
kubectl get pods -n rapidreach

echo ""
echo "Access points:"
echo "  Device Server: http://192.168.2.79:30002"
echo "  Web App:       http://192.168.2.79:30080"
EOF

echo ""
echo "Done!"

