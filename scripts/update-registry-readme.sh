#!/bin/bash

# Script to update the README in the device-registry repository
# Usage: GITHUB_TOKEN=your_token ./update-registry-readme.sh
#    or: export GITHUB_TOKEN=your_token
#        ./update-registry-readme.sh

# Check if GITHUB_TOKEN is set
if [ -z "$GITHUB_TOKEN" ]; then
    echo "Error: GITHUB_TOKEN environment variable is not set"
    echo "Usage: GITHUB_TOKEN=your_token ./update-registry-readme.sh"
    exit 1
fi

REPO_OWNER="enera-international"
REPO_NAME="device-registry"

# First, get the current README to check if it exists and get its SHA
echo "Checking for existing README..."
RESPONSE=$(curl -s -H "Authorization: token ${GITHUB_TOKEN}" \
    -H "Accept: application/vnd.github.v3+json" \
    "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/contents/README.md")

# Extract SHA if file exists
SHA=$(echo "$RESPONSE" | grep -o '"sha":[[:space:]]*"[^"]*"' | cut -d'"' -f4)

# Create the updated README content
README_CONTENT=$(cat << 'EOF'
# RapidReach Device Registry

This repository serves as a centralized device registry for RapidReach IoT devices, providing collision-free device ID assignment.

## 🎯 Purpose

- **Prevent ID Collisions**: Ensures each device gets a unique identifier
- **Track Registered Devices**: Maintains a record of all deployed devices
- **Automatic ID Extension**: Extends ID length when collisions are detected
- **Version Control**: Full history of device registrations

## 📁 Repository Structure

```
devices/
├── 313938.json          # Standard 6-character ID
├── 31393834.json        # 8-character ID (collision resolved)
├── 313938343233.json    # 12-character ID (multiple collisions)
└── ...
```

## 📄 Device Registration Format

Each device registration is stored as a JSON file:

```json
{
  "deviceId": "313938",
  "fullHardwareId": "313938343233510e003d0029",
  "type": "speaker",
  "registeredAt": "2025-01-15T10:30:00Z",
  "firmwareVersion": "1.0.0",
  "metadata": {
    "boardRevision": "R02",
    "registrationMethod": "automatic"
  }
}
```

## 🔄 Collision Resolution Algorithm

When a device attempts to register:

1. **Initial Attempt**: Tries preferred length (default: 6 characters)
2. **Collision Check**: Checks if `devices/{id}.json` exists
3. **Extension**: If collision detected, extends to 7, 8, 9... characters
4. **Registration**: Creates file with the first available unique ID
5. **Usage**: Device uses the assigned ID for all communications

### Example Flow:
```
Device ID: 313938343233510e003d0029
├─ Try: 313938     → Exists! (collision)
├─ Try: 3139383    → Exists! (collision)
├─ Try: 31393834   → Available!
└─ Registered as: 31393834
```

## 🚀 Integration with RapidReach Firmware

### Configuration in `prj.conf`:
```conf
CONFIG_RPR_MODULE_DEVICE_REGISTRY=y
CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN="your_github_token_here"
CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER="enera-international"
CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME="device-registry"
CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH=6
```

### Firmware Behavior:
- On first boot, device attempts to register
- If successful, uses assigned ID for MQTT client ID
- If GitHub unavailable, falls back to default 6-char prefix
- ID is persistent across reboots

## 📊 Statistics

To get registry statistics:
```bash
# Count total devices
find devices -name "*.json" | wc -l

# Count by ID length
find devices -name "??????.json" | wc -l  # 6-char IDs
find devices -name "???????.json" | wc -l # 7-char IDs

# Recent registrations
ls -lt devices/*.json | head -10
```

## 🔐 Security Considerations

- **Token Security**: GitHub token should be stored securely in firmware
- **Repository Access**: Consider making this repo private for production
- **Rate Limits**: GitHub API allows 5000 requests/hour with token
- **Backup**: Regular backups recommended for production deployments

## 🛠️ Manual Management

### Register a device manually:
```bash
# Create a device registration
echo '{
  "deviceId": "AABBCC",
  "fullHardwareId": "AABBCC112233445566778899",
  "type": "speaker",
  "registeredAt": "'$(date -u +%Y-%m-%dT%H:%M:%SZ)'",
  "firmwareVersion": "1.0.0"
}' > devices/AABBCC.json

# Commit and push
git add devices/AABBCC.json
git commit -m "Register device AABBCC"
git push
```

### Remove a device:
```bash
git rm devices/XXXXXX.json
git commit -m "Deregister device XXXXXX"
git push
```

## 📈 Future Enhancements

- [ ] Web dashboard for device management
- [ ] Automatic cleanup of old/inactive devices
- [ ] Device metadata enrichment
- [ ] Geographic distribution tracking
- [ ] Health monitoring integration

## 📞 Support

For issues or questions:
- Create an issue in this repository
- Contact the RapidReach team

---
*This registry is automatically maintained by RapidReach firmware v1.0.0+*
EOF
)

# Base64 encode the README
README_BASE64=$(echo -n "$README_CONTENT" | base64 -w 0)

# Prepare the API request
if [ -n "$SHA" ]; then
    echo "Updating existing README..."
    REQUEST_BODY="{
        \"message\": \"Update README with comprehensive documentation\",
        \"content\": \"${README_BASE64}\",
        \"sha\": \"${SHA}\"
    }"
else
    echo "Creating new README..."
    REQUEST_BODY="{
        \"message\": \"Add comprehensive README documentation\",
        \"content\": \"${README_BASE64}\"
    }"
fi

# Send the request
RESPONSE=$(curl -s -X PUT \
    -H "Authorization: token ${GITHUB_TOKEN}" \
    -H "Accept: application/vnd.github.v3+json" \
    "https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/contents/README.md" \
    -d "${REQUEST_BODY}")

# Check if successful
if echo "$RESPONSE" | grep -q '"content"'; then
    echo "✅ README updated successfully!"
    echo "View at: https://github.com/${REPO_OWNER}/${REPO_NAME}/blob/main/README.md"
else
    echo "❌ Failed to update README"
    echo "Response: $RESPONSE"
fi
