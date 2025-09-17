#!/bin/bash
set -e

# Build and push only web-app
REGISTRY="${DOCKER_REGISTRY:-enerahub}"
VERSION="${1:-latest}"

echo "Building web-app with registry: ${REGISTRY}"
echo "Tag: ${VERSION}"

cd web-app

# Create production Dockerfile
cat > Dockerfile.prod << 'EOF'
# Build stage
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
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

# Create nginx.conf if it doesn't exist
if [ ! -f nginx.conf ]; then
    cat > nginx.conf << 'EOF'
server {
    listen 80;
    server_name localhost;
    root /usr/share/nginx/html;
    index index.html;

    location / {
        try_files $uri $uri/ /index.html;
    }

    location /api {
        proxy_pass http://device-server:3001;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}
EOF
fi

# Build the image
docker build -f Dockerfile.prod -t ${REGISTRY}/web-app:${VERSION} \
  --build-arg VITE_API_URL=http://192.168.2.79:30002/api \
  --build-arg VITE_API_KEY=dev-api-key-12345 .

docker tag ${REGISTRY}/web-app:${VERSION} ${REGISTRY}/web-app:latest

echo "Web app built successfully!"

# Push if requested
if [ "$2" = "--push" ]; then
    echo "Pushing to registry..."
    docker push ${REGISTRY}/web-app:${VERSION}
    docker push ${REGISTRY}/web-app:latest
    echo "Push complete!"
fi
