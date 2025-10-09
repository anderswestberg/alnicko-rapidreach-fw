#!/bin/bash

# Build script for RapidReach Docker images
# Run this on your development machine to build and push images

set -e

# Configuration
# Set REGISTRY environment variable or edit this line
# Examples:
#   REGISTRY=myusername                    # Docker Hub
#   REGISTRY=ghcr.io/myusername           # GitHub Container Registry
#   REGISTRY=192.168.2.62:5000            # Local registry
#   REGISTRY=enerahub                    # Default (requires Docker Hub account)
REGISTRY="${DOCKER_REGISTRY:-enerahub}"
VERSION="${1:-latest}"

if [ "$REGISTRY" = "enerahub" ]; then
    echo "⚠️  Warning: Using default registry 'enerahub'"
    echo "   To use your own registry, set DOCKER_REGISTRY environment variable:"
    echo "   export DOCKER_REGISTRY=yourusername"
    echo ""
fi

echo "Building RapidReach images with registry: ${REGISTRY}"
echo "Tag: ${VERSION}"

# Build device-server
echo "Building device-server..."
cd ../device-server
docker build -t ${REGISTRY}/device-server:${VERSION} .
docker tag ${REGISTRY}/device-server:${VERSION} ${REGISTRY}/device-server:latest

# Build web-app
echo "Building web-app..."
cd ../web-app
docker build -t ${REGISTRY}/web-app:${VERSION} .
docker tag ${REGISTRY}/web-app:${VERSION} ${REGISTRY}/web-app:latest

echo ""
echo "Images built successfully!"
echo ""
echo "To push to a registry:"
echo "  docker push ${REGISTRY}/device-server:${VERSION}"
echo "  docker push ${REGISTRY}/device-server:latest"
echo "  docker push ${REGISTRY}/web-app:${VERSION}"
echo "  docker push ${REGISTRY}/web-app:latest"
echo ""
echo "Or save for transfer:"
echo "  docker save ${REGISTRY}/device-server:latest ${REGISTRY}/web-app:latest | gzip > rapidreach-images.tar.gz"
echo "  # On k3s host: docker load < rapidreach-images.tar.gz"
