# Secrets Management Guide

This document explains how to manage secrets and sensitive credentials for the RapidReach project.

## Quick Start

1. Create a `.env` file in the project root (this file is git-ignored)
2. Copy the following template and fill in your actual values:

```bash
# EMQX Management API
EMQX_USERNAME=<EMQX_USERNAME>
EMQX_PASSWORD=your_actual_emqx_password_here

# OpenAI API
OPENAI_API_KEY=<OPENAI_API_KEY>

# Jira API
JIRA_USERNAME=anders.westberg.ai
JIRA_PASSWORD=your_actual_jira_password_here
JIRA_URL=http://jira:8080

# MQTT Broker (these can stay as-is for development)
MQTT_HOST=localhost
MQTT_PORT=1883
MQTT_USERNAME=admin
MQTT_PASSWORD=public

# Device Server API
DEVICE_SERVER_API_KEY=your-secure-api-key-here
```

## Using Secrets in Different Components

### 1. Shell Scripts
```bash
# Load environment variables
source .env

# Use in scripts
curl -u $EMQX_USERNAME:$EMQX_PASSWORD http://localhost:18083/api/v5/clients
```

### 2. Docker Compose
The `device-servers-compose.yml` already references environment variables:
```yaml
environment:
  - API_KEY=${DEVICE_SERVER_API_KEY:-your-secure-api-key-here}
```

### 3. Web App
Create `web-app/.env.local`:
```
VITE_API_URL=http://localhost:3002/api
VITE_API_KEY=your-secure-api-key-here
```

### 4. Python Scripts (Jira Importer)
```python
import os
from dotenv import load_dotenv

load_dotenv()

jira_username = os.getenv('JIRA_USERNAME')
jira_password = os.getenv('JIRA_PASSWORD')
```

## Security Best Practices

1. **Never commit `.env` files** - They are git-ignored for a reason
2. **Use strong, unique passwords** - Don't reuse passwords across services
3. **Rotate credentials regularly** - Especially API keys
4. **Limit access** - Only share credentials with team members who need them
5. **Use environment-specific files** - `.env.development`, `.env.production`, etc.

## Sharing Secrets with Team Members

For team collaboration, use one of these methods:

1. **Secure messaging** - Share via encrypted channels (Signal, encrypted email)
2. **Password manager** - Use a team password manager (1Password, Bitwarden)
3. **Secret management service** - For production, consider:
   - HashiCorp Vault
   - AWS Secrets Manager
   - Azure Key Vault
   - Google Secret Manager

## Troubleshooting

### GitHub Push Rejected
If GitHub rejects your push due to detected secrets:
1. Remove the secrets from committed files
2. Replace with placeholders or environment variable references
3. Commit the changes
4. The secrets should go in `.env` files instead

### Missing Credentials
If a service isn't working:
1. Check if `.env` file exists
2. Verify all required variables are set
3. Check if the service is loading the environment correctly

## Required Secrets Reference

| Secret | Used By | Purpose |
|--------|---------|---------|
| EMQX_USERNAME/PASSWORD | MQTT management scripts | EMQX admin API access |
| OPENAI_API_KEY | AI assistant features | GPT model access |
| JIRA_USERNAME/PASSWORD | Jira importer | Issue synchronization |
| DEVICE_SERVER_API_KEY | Web app, device server | API authentication |

## Contact

Store the actual credentials in your password manager or contact the team lead for access.
