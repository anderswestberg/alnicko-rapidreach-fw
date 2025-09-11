# React Admin Enterprise Authentication

The web-app uses React Admin Enterprise packages which require authentication to Marmelab's private registry.

## Option 1: Add Authentication Token (If you have a license)

1. Get your authentication token from https://marmelab.com/ra-enterprise/
2. Create or update the `.npmrc` file in the web-app directory:

```bash
cd ../web-app
cat > .npmrc << EOF
@react-admin:registry=https://registry.marmelab.com
//registry.marmelab.com/:_authToken=YOUR_AUTH_TOKEN_HERE
EOF
```

3. Build the images:
```bash
cd ../k8s
./build-images.sh
```

## Option 2: Use Open Source React Admin (No license needed)

If you don't have a React Admin Enterprise license, you can modify the web-app to use only open-source packages:

1. Remove enterprise packages from `package.json`:
```json
// Remove these lines:
"@react-admin/ra-ai": "^5.3.0",
"@react-admin/ra-audit-log": "^6.2.0",
"@react-admin/ra-editable-datagrid": "^5.2.2",
"@react-admin/ra-enterprise": "^12.1.0",
"@react-admin/ra-form-layout": "^5.15.0",
"@react-admin/ra-markdown": "^5.1.1",
"@react-admin/ra-navigation": "^6.1.0",
"@react-admin/ra-rbac": "^6.2.0",
"@react-admin/ra-realtime": "^5.1.1",
"@react-admin/ra-relationships": "^5.3.1",
"@react-admin/ra-search": "^5.4.0",
"@react-admin/ra-tree": "^7.0.2"
```

2. Update imports in the code to remove enterprise features
3. Delete `package-lock.json` and reinstall:
```bash
rm package-lock.json
npm install --legacy-peer-deps
```

## Option 3: Build Without Private Registry (Quick workaround)

For development/testing, you can skip the web-app build:

```bash
# Build only device-server
cd ../device-server
docker build -t enerahub/device-server:latest .
docker push enerahub/device-server:latest

# Use a pre-built web-app image or nginx with static files
```

## Storing Auth Token Securely

For production builds:

1. Use Docker build secrets:
```bash
# Create secret file
echo "//registry.marmelab.com/:_authToken=YOUR_TOKEN" > ~/.npmrc-marmelab

# Build with secret
docker build --secret id=npmrc,src=$HOME/.npmrc-marmelab -t enerahub/web-app:latest .
```

2. In Dockerfile, use the secret:
```dockerfile
RUN --mount=type=secret,id=npmrc,target=/root/.npmrc \
    npm ci --legacy-peer-deps
```

3. Or use CI/CD environment variables to inject the token during build
