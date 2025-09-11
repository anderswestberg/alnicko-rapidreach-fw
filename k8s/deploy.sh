#!/bin/bash

# Quick deployment script for RapidReach on k3s
set -e

YELLOW='\033[1;33m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${YELLOW}=== RapidReach k3s Deployment ===${NC}\n"

# Check if kubectl is available
if ! command -v kubectl &> /dev/null; then
    echo -e "${RED}kubectl is not installed or not in PATH${NC}"
    exit 1
fi

# Check if we can connect to cluster
if ! kubectl cluster-info &> /dev/null; then
    echo -e "${RED}Cannot connect to Kubernetes cluster${NC}"
    echo "Make sure you have the correct kubeconfig set"
    exit 1
fi

echo -e "${GREEN}✓ Connected to cluster${NC}"

# Deploy using kustomization
echo -e "\n${YELLOW}Deploying RapidReach services...${NC}"
kubectl apply -k .

# Wait for namespace
kubectl wait --for=condition=Established namespace/rapidreach --timeout=30s

echo -e "\n${YELLOW}Waiting for pods to be ready...${NC}"

# Wait for deployments
kubectl wait --namespace rapidreach \
  --for=condition=available \
  --timeout=300s \
  deployment/emqx \
  deployment/mongodb \
  deployment/device-server \
  deployment/web-app

echo -e "\n${GREEN}✓ All deployments ready!${NC}"

# Show services
echo -e "\n${YELLOW}Services:${NC}"
kubectl get svc -n rapidreach

# Get MQTT LoadBalancer IP
MQTT_IP=$(kubectl get svc emqx -n rapidreach -o jsonpath='{.status.loadBalancer.ingress[0].ip}' 2>/dev/null || echo "pending")

echo -e "\n${GREEN}=== Deployment Complete! ===${NC}"
echo -e "\nAccess points:"
echo -e "  ${GREEN}MQTT Broker:${NC} ${MQTT_IP:-192.168.2.62}:1883"
echo -e "  ${GREEN}MQTT Dashboard:${NC} http://${MQTT_IP:-192.168.2.62}:18083 (admin/public)"
echo -e "  ${GREEN}Device Server:${NC} http://192.168.2.62:30002"
echo -e "  ${GREEN}Web App:${NC} http://192.168.2.62:30080"
echo -e "\n${YELLOW}View logs:${NC}"
echo -e "  kubectl logs -n rapidreach -l app=device-server -f"
echo -e "  kubectl logs -n rapidreach -l app=emqx -f"
