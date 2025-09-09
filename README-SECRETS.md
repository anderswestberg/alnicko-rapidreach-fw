# Securing Sensitive Configuration

This project uses a separate configuration file for sensitive values like API tokens.

## Setup Instructions

1. Copy the template file:
   ```bash
   cp prj-secrets.conf.template prj-secrets.conf
   ```

2. Edit `prj-secrets.conf` and add your GitHub token:
   ```conf
   CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN="your-actual-token-here"
   ```

3. The secrets file is automatically loaded by the build system when present.

## Building with Secrets

When you run `west build`, Zephyr will automatically merge:
- `prj.conf` (main configuration)
- `prj-secrets.conf` (if it exists)
- Any board-specific configurations

## Security Notes

- `prj-secrets.conf` is listed in `.gitignore` and will NOT be committed
- Never commit actual tokens to version control
- For production, consider using:
  - Environment variables during build
  - Secure key storage in hardware
  - Build-time secret injection in CI/CD

## Alternative Methods

### Using Environment Variables

You can also pass the token during build:
```bash
west build -DCONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN=\"your-token\"
```

### Using CONF_FILE

For multiple configuration files:
```bash
west build -- -DCONF_FILE="prj.conf;prj-secrets.conf"
```
