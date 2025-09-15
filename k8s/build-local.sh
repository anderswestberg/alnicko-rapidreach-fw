#!/bin/bash

# Build script for local k3s deployment (no registry needed)
set -e

VERSION="${1:-latest}"
K3S_HOST="${2:-192.168.2.62}"
K3S_USER="${3:-$USER}"

echo "Building RapidReach images for direct k3s import..."

# Build device-server
echo "Building device-server..."
cd ../device-server
docker build -t rapidreach/device-server:${VERSION} .

# Build web-app
echo "Building web-app..."
cd ../web-app

# Create nginx config if needed
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
}
EOF
fi

# Create production Dockerfile
cat > Dockerfile.prod << 'EOF'
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
# Copy .npmrc if it exists for private registry authentication
COPY .npmrc* ./
RUN npm ci --legacy-peer-deps
COPY . .
ARG VITE_API_URL=http://192.168.2.79:30002/api
ARG VITE_API_KEY=dev-api-key-12345
RUN npm run build

FROM nginx:alpine
COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
EOF

docker build -f Dockerfile.prod -t rapidreach/web-app:${VERSION} .

echo ""
echo "Images built successfully!"
echo ""
echo "Transferring to k3s host..."

# Save and transfer images
docker save rapidreach/device-server:${VERSION} rapidreach/web-app:${VERSION} | gzip > /tmp/rapidreach-images.tar.gz

echo "Copying to ${K3S_USER}@${K3S_HOST}..."
scp /tmp/rapidreach-images.tar.gz ${K3S_USER}@${K3S_HOST}:~/

echo "Importing images on k3s host..."
ssh ${K3S_USER}@${K3S_HOST} "sudo k3s ctr images import ~/rapidreach-images.tar.gz && rm ~/rapidreach-images.tar.gz"

rm /tmp/rapidreach-images.tar.gz

echo ""
echo "✅ Images deployed to k3s!"
echo ""
echo "To deploy the application:"
echo "  ssh ${K3S_USER}@${K3S_HOST}"
echo "  cd <path-to-k8s-manifests>"
echo "  kubectl apply -k ."
