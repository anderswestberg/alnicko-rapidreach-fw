# KEY = your API key; SECRET = your API secret
curl -s \
  -u <EMQX_USERNAME>:<EMQX_PASSWORD> \
  -H 'Accept: application/json' \
  http://localhost:18083/api/v5/clients | jq

