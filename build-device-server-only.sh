#!/bin/bash
set -e

# Build and push only device-server
REGISTRY="${DOCKER_REGISTRY:-enerahub}"
VERSION="${1:-latest}"

echo "Building device-server with registry: ${REGISTRY}"
echo "Tag: ${VERSION}"

cd device-server
docker build -t ${REGISTRY}/device-server:${VERSION} .
docker tag ${REGISTRY}/device-server:${VERSION} ${REGISTRY}/device-server:latest

echo "Device server built successfully!"

# Push if requested
if [ "$2" = "--push" ]; then
    echo "Pushing to registry..."
    docker push ${REGISTRY}/device-server:${VERSION}
    docker push ${REGISTRY}/device-server:latest
    echo "Push complete!"
fi
