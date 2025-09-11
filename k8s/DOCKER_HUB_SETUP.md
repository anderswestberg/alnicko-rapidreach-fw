# Docker Hub Setup for EneraHub

## 1. Login to Docker Hub

### Option A: Interactive Login
```bash
docker login
# Enter username: enerahub
# Enter password: <your-password>
```

### Option B: Using Personal Access Token (Recommended)
1. Go to https://hub.docker.com/settings/security
2. Create a new access token with "Read, Write, Delete" permissions
3. Copy the token and use it:
```bash
docker login -u enerahub
# Enter password: <paste-your-token>
```

### Option C: Login with Password in Command (Less Secure)
```bash
echo "your-password" | docker login -u enerahub --password-stdin
```

## 2. Create Repositories on Docker Hub

Go to https://hub.docker.com/ and create two repositories:
- `enerahub/device-server`
- `enerahub/web-app`

Or use Docker Hub CLI:
```bash
# Install hub-tool if needed
wget https://github.com/docker/hub-tool/releases/download/v0.4.5/hub-tool-linux-amd64.tar.gz
tar -xzf hub-tool-linux-amd64.tar.gz
sudo mv hub-tool /usr/local/bin/

# Login and create repos
hub-tool login
hub-tool repo create enerahub/device-server --private
hub-tool repo create enerahub/web-app --private
```

## 3. Build and Push Images

```bash
cd k8s

# The registry is already updated to enerahub
# Build images
export DOCKER_REGISTRY=enerahub
./build-images.sh

# Push to Docker Hub
docker push enerahub/device-server:latest
docker push enerahub/web-app:latest

# Or use the build script's output commands
```

## 4. Deploy to k3s

On your k3s host:
```bash
# If repositories are private, k3s needs to login too
sudo k3s crictl pull --creds enerahub:<password> docker.io/enerahub/device-server:latest
sudo k3s crictl pull --creds enerahub:<password> docker.io/enerahub/web-app:latest

# Or create a pull secret
kubectl create secret docker-registry regcred \
  --docker-server=docker.io \
  --docker-username=enerahub \
  --docker-password=<password> \
  --docker-email=<email> \
  -n rapidreach

# Then add to deployments:
# imagePullSecrets:
# - name: regcred
```

## 5. Verify Login

```bash
# Check if you're logged in
docker info | grep Username

# Test push (creates a test tag)
docker tag enerahub/device-server:latest enerahub/device-server:test
docker push enerahub/device-server:test
docker rmi enerahub/device-server:test
```

## Security Notes

1. **Don't commit credentials**: Never put passwords in scripts or git
2. **Use tokens**: Personal Access Tokens are safer than passwords
3. **Logout when done**: `docker logout` when finished
4. **Consider private repos**: Make repos private if they contain sensitive code

## Troubleshooting

### "unauthorized: authentication required"
- You're not logged in: Run `docker login`

### "denied: requested access to the resource is denied"
- Wrong username or repository doesn't exist
- Check: `docker images | grep enerahub`

### Rate Limits
- Docker Hub has pull rate limits
- Authenticated users get higher limits (200 pulls/6hr)
- Consider using a local registry for k3s if hitting limits
