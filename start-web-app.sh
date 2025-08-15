#!/bin/bash

# Script to start the RapidReach web app and services

set -e

echo "🚀 Starting RapidReach Services..."

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo "❌ Docker is not running. Please start Docker first."
    exit 1
fi

# Start backend services
echo "📦 Starting backend services..."
docker compose -f device-servers-compose.yml up -d emqx log-server device-server

# Wait for services to be ready
echo "⏳ Waiting for services to start..."
sleep 5

# Check service health
echo "🔍 Checking service health..."
if curl -s http://localhost:3002/health > /dev/null; then
    echo "✅ Device server is healthy"
else
    echo "❌ Device server is not responding"
fi

# Start web app in development mode
echo "🌐 Starting web app..."
cd web-app
npm run dev &

echo "
✨ RapidReach services are starting!

Access points:
- Web App: http://localhost:5173
- Device Server API: http://localhost:3002
- Log Server: http://localhost:3001
- EMQX Dashboard: http://localhost:18083 (admin/public)

Press Ctrl+C to stop all services
"

# Wait for interrupt
wait
