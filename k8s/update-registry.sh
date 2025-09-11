#!/bin/bash

# Script to update image registry in k8s manifests
# Usage: ./update-registry.sh myusername

NEW_REGISTRY="${1}"

if [ -z "$NEW_REGISTRY" ]; then
    echo "Usage: $0 <new-registry>"
    echo ""
    echo "Examples:"
    echo "  $0 myusername                # Docker Hub"
    echo "  $0 ghcr.io/myusername        # GitHub Container Registry"
    echo "  $0 192.168.2.62:5000         # Local registry"
    exit 1
fi

echo "Updating registry from 'rapidreach' to '${NEW_REGISTRY}' in all YAML files..."

# Update deployment files
sed -i "s|image: rapidreach/|image: ${NEW_REGISTRY}/|g" device-server.yaml web-app.yaml

# Update kustomization.yaml
sed -i "s|name: rapidreach/|name: ${NEW_REGISTRY}/|g" kustomization.yaml

echo "✅ Updated registry in manifests"
echo ""
echo "Next steps:"
echo "1. Build images with: DOCKER_REGISTRY=${NEW_REGISTRY} ./build-images.sh"
echo "2. Push images to your registry"
echo "3. Deploy with: kubectl apply -k ."
