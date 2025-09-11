# RapidReach k3s Setup Guide

This guide will help you deploy RapidReach on a k3s cluster with a fixed IP address (192.168.2.62) for the MQTT broker.

## Prerequisites

- VM or physical machine with:
  - Ubuntu 20.04+ or similar
  - Fixed IP: 192.168.2.62
  - 4GB+ RAM
  - 20GB+ disk space
  - SSH access

## Quick Setup

### 1. Install k3s on Target Host

```bash
# SSH to your k3s host
ssh user@192.168.2.62

# Install k3s without traefik (we'll use NodePort/LoadBalancer)
curl -sfL https://get.k3s.io | sh -s - --disable traefik

# Get kubeconfig for remote access
sudo cat /etc/rancher/k3s/k3s.yaml

# On your local machine, save this as ~/.kube/config-rapidreach
# Update the server URL to: https://192.168.2.62:6443
```

### 2. Install MetalLB (for LoadBalancer support)

```bash
# Apply MetalLB
kubectl apply -f https://raw.githubusercontent.com/metallb/metallb/v0.13.7/config/manifests/metallb-native.yaml

# Wait for MetalLB to be ready
kubectl wait --namespace metallb-system \
  --for=condition=ready pod \
  --selector=app=metallb \
  --timeout=90s

# Apply MetalLB configuration
kubectl apply -f metallb-config.yaml
```

### 3. Build Docker Images (on dev machine)

#### Option A: Direct k3s Import (No Registry Needed)
```bash
# Use the local build script
chmod +x build-local.sh
./build-local.sh latest 192.168.2.62 yourusername

# This will:
# 1. Build images locally
# 2. Transfer to k3s host
# 3. Import directly into k3s
```

#### Option B: Use Docker Registry
```bash
# First, update manifests to use your registry
./update-registry.sh yourdockerhubusername

# Build with your registry
export DOCKER_REGISTRY=yourdockerhubusername
./build-images.sh

# Push to registry
docker push ${DOCKER_REGISTRY}/device-server:latest
docker push ${DOCKER_REGISTRY}/web-app:latest
```

#### Option C: Manual Transfer
```bash
# Build with default names
./build-images.sh

# Save images for transfer
docker save rapidreach/device-server:latest rapidreach/web-app:latest | gzip > rapidreach-images.tar.gz

# Transfer to k3s host
scp rapidreach-images.tar.gz user@192.168.2.62:~/

# On k3s host, import images
ssh user@192.168.2.62
sudo k3s ctr images import rapidreach-images.tar.gz
```

### 4. Deploy RapidReach

```bash
# Apply all manifests
kubectl apply -f namespace.yaml
kubectl apply -f mqtt-broker.yaml
kubectl apply -f mongodb.yaml
kubectl apply -f device-server.yaml
kubectl apply -f web-app.yaml

# Or use kustomization
kubectl apply -k .
```

### 5. Verify Deployment

```bash
# Check all pods are running
kubectl get pods -n rapidreach

# Check services
kubectl get svc -n rapidreach

# Check MQTT LoadBalancer IP
kubectl get svc emqx -n rapidreach -o jsonpath='{.status.loadBalancer.ingress[0].ip}'
```

## Access Points

| Service | URL | Notes |
|---------|-----|-------|
| MQTT Broker | `192.168.2.62:1883` | Fixed IP via MetalLB |
| MQTT Dashboard | `http://192.168.2.62:18083` | Username: admin, Password: public |
| Device Server API | `http://192.168.2.62:30002` | NodePort |
| Web App | `http://192.168.2.62:30080` | NodePort |

## Device Configuration

Your RapidReach devices should connect to:
- MQTT Host: `192.168.2.62`
- MQTT Port: `1883`
- Username: `admin`
- Password: `public`

## Monitoring

```bash
# View logs
kubectl logs -n rapidreach -l app=device-server -f
kubectl logs -n rapidreach -l app=emqx -f

# Check resource usage
kubectl top nodes
kubectl top pods -n rapidreach

# Access k3s dashboard (optional)
kubectl apply -f https://raw.githubusercontent.com/kubernetes/dashboard/v2.7.0/aio/deploy/recommended.yaml
```

## Updating Services

```bash
# Update device-server
docker build -t rapidreach/device-server:v1.1 ../device-server
docker save rapidreach/device-server:v1.1 | ssh user@192.168.2.62 sudo k3s ctr images import -
kubectl set image deployment/device-server device-server=rapidreach/device-server:v1.1 -n rapidreach

# Watch rollout
kubectl rollout status deployment/device-server -n rapidreach
```

## Backup

```bash
# Backup MongoDB data
kubectl exec -n rapidreach mongodb-0 -- mongodump --out=/tmp/backup
kubectl cp rapidreach/mongodb-0:/tmp/backup ./mongo-backup

# Backup persistent volumes
kubectl get pv
```

## Troubleshooting

### MQTT Connection Issues
```bash
# Test MQTT from k3s host
sudo apt-get install mosquitto-clients
mosquitto_sub -h localhost -p 1883 -u admin -P public -t '#' -v

# Check MQTT logs
kubectl logs -n rapidreach -l app=emqx --tail=50
```

### LoadBalancer Not Getting IP
```bash
# Check MetalLB controller
kubectl logs -n metallb-system -l app=metallb,component=controller

# Check L2 advertisements
kubectl get l2advertisements.metallb.io -n metallb-system
```

### Device Can't Connect
1. Check firewall rules on k3s host
2. Verify MQTT is accessible: `nc -zv 192.168.2.62 1883`
3. Check device logs via serial console

## Adding More Services

Create a new deployment:
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: my-service
  namespace: rapidreach
spec:
  # ... deployment spec
```

Expose with LoadBalancer:
```yaml
apiVersion: v1
kind: Service
metadata:
  name: my-service
  namespace: rapidreach
  annotations:
    metallb.universe.tf/loadBalancerIPs: "192.168.2.63"
spec:
  type: LoadBalancer
  # ... service spec
```

## Scaling

For production:
1. Add more k3s nodes: `k3s agent --server https://192.168.2.62:6443 --token <token>`
2. Increase replicas: `kubectl scale deployment/device-server --replicas=3 -n rapidreach`
3. Add MongoDB replica set
4. Use external storage (NFS, Longhorn, etc.)
