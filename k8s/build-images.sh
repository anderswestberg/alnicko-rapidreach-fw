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

# Build web-app (multi-stage build for production)
echo "Building web-app..."
cd ../web-app
cat > Dockerfile.prod << 'EOF'
# Build stage
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
# Copy .npmrc if it exists for private registry authentication
COPY .npmrc* ./
RUN npm ci --legacy-peer-deps
COPY . .
ARG VITE_API_URL
ARG VITE_API_KEY
RUN npm run build

# Production stage
FROM nginx:alpine
COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
EOF

# Create nginx config if it doesn't exist
if [ ! -f nginx.conf ]; then
cat > nginx.conf << 'EOF'
server {
    listen 80;
    server_name _;
    root /usr/share/nginx/html;
    index index.html;

    location / {
        try_files $uri $uri/ /index.html;
    }

    location /api {
        proxy_pass http://device-server:3002;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}
EOF
fi

docker build -f Dockerfile.prod -t ${REGISTRY}/web-app:${VERSION} \
  --build-arg VITE_API_URL=http://192.168.2.79:30002/api \
  --build-arg VITE_API_KEY=dev-api-key-12345 .
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
