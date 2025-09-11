#!/bin/bash

# Script to create Docker Hub pull secret for k3s
# This is needed if your enerahub repositories are private

echo "This script will create a Kubernetes secret for pulling private Docker images"
echo ""

read -p "Docker Hub Username [enerahub]: " DOCKER_USER
DOCKER_USER=${DOCKER_USER:-enerahub}

read -p "Docker Hub Email: " DOCKER_EMAIL
read -s -p "Docker Hub Password/Token: " DOCKER_PASS
echo ""

echo ""
echo "Creating secret 'regcred' in namespace 'rapidreach'..."

# Ensure namespace exists
kubectl create namespace rapidreach 2>/dev/null || true

# Create the secret
kubectl create secret docker-registry regcred \
  --docker-server=docker.io \
  --docker-username="${DOCKER_USER}" \
  --docker-password="${DOCKER_PASS}" \
  --docker-email="${DOCKER_EMAIL}" \
  --namespace=rapidreach \
  --dry-run=client -o yaml | kubectl apply -f -

echo ""
echo "✅ Secret created!"
echo ""
echo "To use this secret, add the following to your deployments:"
echo ""
echo "    spec:"
echo "      imagePullSecrets:"
echo "      - name: regcred"
echo ""
echo "The manifests already include this if you uncomment the lines."
