#!/bin/bash

# Script to set up GitHub repository for device registry
# Usage: ./setup-device-registry.sh <github-token> <repo-owner> <repo-name>

set -e

GITHUB_TOKEN="${1}"
REPO_OWNER="${2:-rapidreach}"
REPO_NAME="${3:-device-registry}"

if [ -z "$GITHUB_TOKEN" ]; then
    echo "Error: GitHub token is required"
    echo "Usage: $0 <github-token> [repo-owner] [repo-name]"
    echo ""
    echo "Create a token at: https://github.com/settings/tokens"
    echo "Required scopes: repo (full control)"
    exit 1
fi

echo "Setting up device registry repository..."

# Create repository
echo "Creating repository ${REPO_OWNER}/${REPO_NAME}..."
curl -X POST \
    -H "Authorization: token ${GITHUB_TOKEN}" \
    -H "Accept: application/vnd.github.v3+json" \
    https://api.github.com/user/repos \
    -d "{
        \"name\": \"${REPO_NAME}\",
        \"description\": \"RapidReach device registry for collision-free ID assignment\",
        \"private\": false,
        \"auto_init\": true
    }"

# Wait for repository to be created
sleep 2

# Create devices directory
echo "Creating devices directory..."
curl -X PUT \
    -H "Authorization: token ${GITHUB_TOKEN}" \
    -H "Accept: application/vnd.github.v3+json" \
    "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/contents/devices/.gitkeep" \
    -d "{
        \"message\": \"Initialize devices directory\",
        \"content\": \"\"
    }"

# Create README
echo "Creating README..."
README_CONTENT=$(cat << 'EOF'
# RapidReach Device Registry

This repository serves as a device registry for RapidReach IoT devices.

## Purpose

- Prevent device ID collisions
- Track registered devices
- Provide unique ID assignment

## Structure

```
devices/
├── 313938.json      # 6-character ID
├── 31393834.json    # 8-character ID (collision resolved)
└── ...
```

## Device File Format

```json
{
  "deviceId": "313938",
  "fullHardwareId": "313938343233510e003d0029",
  "type": "speaker",
  "registeredAt": "2025-01-15T10:30:00Z",
  "firmwareVersion": "1.0.0"
}
```

## Collision Resolution

When a device tries to register:
1. It starts with preferred length (e.g., 6 characters)
2. If that ID exists, it tries 7 characters
3. Continues until unique ID is found
4. Device uses the assigned unique ID

## API Usage

Devices register automatically using GitHub API during first boot.
EOF
)

# Base64 encode the README
README_BASE64=$(echo -n "$README_CONTENT" | base64 -w 0)

curl -X PUT \
    -H "Authorization: token ${GITHUB_TOKEN}" \
    -H "Accept: application/vnd.github.v3+json" \
    "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/contents/README.md" \
    -d "{
        \"message\": \"Add README\",
        \"content\": \"${README_BASE64}\"
    }"

echo ""
echo "✅ Device registry repository created successfully!"
echo "Repository URL: https://github.com/${REPO_OWNER}/${REPO_NAME}"
echo ""
echo "To use in firmware, add to prj.conf:"
echo "CONFIG_RPR_MODULE_DEVICE_REGISTRY=y"
echo "CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN=\"${GITHUB_TOKEN}\""
echo "CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER=\"${REPO_OWNER}\""
echo "CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME=\"${REPO_NAME}\""
