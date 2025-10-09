#!/bin/bash
# Build, push to Docker Hub, and deploy to k3s in one command

set -e

# Configuration
REGISTRY="${DOCKER_REGISTRY:-enerahub}"
VERSION="${1:-v$(date +%Y%m%d-%H%M%S)}"
K3S_HOST="${2:-192.168.2.79}"
K3S_USER="${3:-rapidreach}"

echo "=================================="
echo "RapidReach Build & Deploy Pipeline"
echo "=================================="
echo "Registry: $REGISTRY"
echo "Version:  $VERSION"
echo "K3s Host: $K3S_HOST"
echo "K3s User: $K3S_USER"
echo ""

# Step 1: Build images
echo "📦 Step 1/3: Building Docker images..."
export DOCKER_REGISTRY=$REGISTRY
./build-images.sh $VERSION

echo ""
echo "✅ Images built successfully!"
echo ""

# Step 2: Push to Docker Hub
echo "📤 Step 2/3: Pushing to Docker Hub..."
echo "Pushing device-server..."
docker push ${REGISTRY}/device-server:${VERSION}
docker push ${REGISTRY}/device-server:latest

echo "Pushing web-app..."
docker push ${REGISTRY}/web-app:${VERSION}
docker push ${REGISTRY}/web-app:latest

echo ""
echo "✅ Images pushed to Docker Hub!"
echo ""

# Step 3: Deploy to k3s
echo "🚀 Step 3/3: Deploying to k3s cluster..."
./update-k3s-deployments.sh $K3S_HOST $K3S_USER $VERSION

echo ""
echo "=================================="
echo "✅ Deployment Complete!"
echo "=================================="
echo ""
echo "Access points:"
echo "  Web App:     http://${K3S_HOST}:30080"
echo "  API:         http://${K3S_HOST}:30002"
echo "  MQTT Broker: mqtt://${K3S_HOST}:1883"
echo ""
echo "Docker Hub images:"
echo "  ${REGISTRY}/device-server:${VERSION}"
echo "  ${REGISTRY}/web-app:${VERSION}"
echo ""

