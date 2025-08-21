# OpenAI Prompt Proxy (Node.js)

A minimal Express server that exposes a simple REST API to forward prompts to the OpenAI API.

## Setup

1. Install Node.js 18+.
2. Copy env example and set your key:

```bash
cp .env.example .env
# Edit .env to set OPENAI_API_KEY
```

3. Install dependencies (if needed):

```bash
npm install
```

## Run

```bash
npm run start
# or
npm run dev
```

Server starts on PORT (default 3009).

## API

- GET /health → { ok: true }
- POST /prompt
  - Request body (JSON):
    - prompt (string, required)
    - model (string, optional; defaults to env OPENAI_MODEL or gpt-4o-mini)
    - temperature (number, optional, default 0.7)
    - max_tokens (number, optional)
  - Response body (JSON):
    - reply (string)
    - model (string)
    - usage (object from OpenAI)

### Example

```bash
curl -sS -X POST http://localhost:3001/prompt \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"Say hello in one concise sentence."}' | jq
```

## Configuration

Set in .env:

- OPENAI_API_KEY (required)
- PORT (default: 3001)
- OPENAI_MODEL (default: gpt-4o-mini)

## Notes

- This sample uses the Chat Completions API.
- Do not expose your API key to untrusted clients; keep this server-side.
