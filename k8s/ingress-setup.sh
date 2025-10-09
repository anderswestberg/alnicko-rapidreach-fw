#!/bin/bash
# Setup NGINX Ingress and Let's Encrypt for RapidReach

set -e

echo "📦 Installing NGINX Ingress Controller..."
kubectl apply -f https://raw.githubusercontent.com/kubernetes/ingress-nginx/controller-v1.8.2/deploy/static/provider/cloud/deploy.yaml

echo "⏳ Waiting for NGINX Ingress to be ready..."
kubectl wait --namespace ingress-nginx \
  --for=condition=ready pod \
  --selector=app.kubernetes.io/component=controller \
  --timeout=120s

echo ""
echo "📦 Installing cert-manager..."
kubectl apply -f https://github.com/cert-manager/cert-manager/releases/download/v1.13.2/cert-manager.yaml

echo "⏳ Waiting for cert-manager to be ready..."
kubectl wait --namespace cert-manager \
  --for=condition=ready pod \
  --selector=app.kubernetes.io/component=controller \
  --timeout=120s

echo ""
echo "✅ NGINX Ingress and cert-manager installed!"
echo ""
echo "Next steps:"
echo "1. Apply the Let's Encrypt ClusterIssuer"
echo "2. Apply the Ingress resource for speaker-alert.ddns.net"

