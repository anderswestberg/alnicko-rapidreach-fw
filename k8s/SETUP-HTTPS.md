# HTTPS Setup for speaker-alert.ddns.net

## Current Status

✅ **Completed:**
- NGINX Ingress Controller installed
- cert-manager installed with Let's Encrypt ClusterIssuer
- Ingress resource created for speaker-alert.ddns.net
- DNS configured: speaker-alert.ddns.net → 37.123.169.250
- Application deployed: device-server v1.1.0, web-app v1.1.4

⏳ **Pending:**
- Configure NGINX Ingress to use ports 8080/8443 (instead of 80/443)
- Configure router port forwarding: 80→8080, 443→8443

## Issue Encountered

Ports 80 and 443 are blocked by iptables/kube-router rules on the k3s server. Using alternate ports 8080/8443 avoids this issue.

## Solution: Use Ports 8080/8443

### Step 1: Reconfigure NGINX Ingress

Delete and recreate the NGINX Ingress with correct configuration:

```bash
# Save current ingress config
kubectl get ingress rapidreach-ingress -n rapidreach -o yaml > /tmp/ingress-backup.yaml

# Delete existing NGINX Ingress deployment
kubectl delete deployment ingress-nginx-controller -n ingress-nginx

# Install NGINX Ingress with custom ports
helm repo add ingress-nginx https://kubernetes.github.io/ingress-nginx
helm repo update

helm install ingress-nginx ingress-nginx/ingress-nginx \
  --namespace ingress-nginx \
  --set controller.service.type=LoadBalancer \
  --set controller.service.externalIPs={192.168.2.79,37.123.169.250} \
  --set controller.containerPort.http=8080 \
  --set controller.containerPort.https=8443 \
  --set controller.service.ports.http=8080 \
  --set controller.service.ports.https=8443 \
  --set controller.hostPort.enabled=true \
  --set controller.hostPort.ports.http=8080 \
  --set controller.hostPort.ports.https=8443
```

### Step 2: Update Ingress Resource

The Ingress resource doesn't need changes - it works with any ports NGINX is listening on.

### Step 3: Configure Router Port Forwarding

On your router/firewall for public IP 37.123.169.250:

```
External Port 80  → Internal 192.168.2.79:8080  (HTTP)
External Port 443 → Internal 192.168.2.79:8443  (HTTPS)
```

### Step 4: Verify and Test

```bash
# Check NGINX is running
kubectl get pods -n ingress-nginx

# Test from LAN
curl http://192.168.2.79:8080
curl -k https://192.168.2.79:8443

# After router config, test from internet
curl http://speaker-alert.ddns.net
# Should reach the web app

# Check certificate status
kubectl get certificate -n rapidreach
# Should show READY: True after Let's Encrypt validation
```

### Step 5: Access Points

Once configured:
- **HTTP:** http://speaker-alert.ddns.net (redirects to HTTPS)
- **HTTPS:** https://speaker-alert.ddns.net ✅ (with Let's Encrypt SSL)
- **API:** https://speaker-alert.ddns.net/api

## Alternative: Use NodePort (Current Working Setup)

If the above is too complex, the current NodePort setup works:

```
Router forwarding:
37.123.169.250:80  → 192.168.2.79:30330
37.123.169.250:443 → 192.168.2.79:31175

Access:
- Web: http://192.168.2.79:30080 (LAN)
- API: http://192.168.2.79:30002 (LAN)
```

## Troubleshooting

### Check iptables
```bash
sudo iptables -L INPUT -n -v | head -20
```

### Allow ports in iptables
```bash
sudo iptables -I INPUT 1 -p tcp --dport 8080 -j ACCEPT
sudo iptables -I INPUT 1 -p tcp --dport 8443 -j ACCEPT
```

### Save iptables rules
```bash
sudo iptables-save > /etc/iptables/rules.v4
```

## Files Created

- `k8s/ingress-setup.sh` - Install script for NGINX Ingress and cert-manager
- `k8s/letsencrypt-issuer.yaml` - Let's Encrypt ClusterIssuer
- `k8s/rapidreach-ingress.yaml` - Ingress resource
- `k8s/SETUP-HTTPS.md` - This file

## Next Session

1. Clean up failed NGINX Ingress deployments
2. Reinstall with Helm using custom ports 8080/8443
3. Configure router port forwarding
4. Verify Let's Encrypt certificate issuance
5. Update web app to use HTTPS API URL


