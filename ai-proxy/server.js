require('dotenv').config();

const express = require('express');
const cors = require('cors');
const morgan = require('morgan');
const OpenAI = require('openai');

const app = express();

// Middleware
app.use(cors());
app.use(express.json({ limit: '1mb' }));
app.use(morgan('dev'));

// OpenAI client
const openAIConfig = {
  apiKey: process.env.OPENAI_API_KEY,
};
if (process.env.OPENAI_ORG) {
  openAIConfig.organization = process.env.OPENAI_ORG;
}
// Only set project if it looks like a valid project id (proj_...)
if (process.env.OPENAI_PROJECT) {
  openAIConfig.project = process.env.OPENAI_PROJECT;
}
const openai = new OpenAI(openAIConfig);
const defaultModel = process.env.OPENAI_MODEL || 'gpt-4o-mini';

// Health check
app.get('/health', (req, res) => {
	return res.json({ ok: true });
});

// Forward prompt to OpenAI
app.post('/prompt', async (req, res) => {
	try {
		const { prompt, model, temperature, max_tokens } = req.body || {};
		if (typeof prompt !== 'string' || prompt.trim().length === 0) {
			return res.status(400).json({ error: 'Field "prompt" (non-empty string) is required.' });
		}

		const lowerPrompt = prompt.toLowerCase();
		const asksNyTime = /new\s*york/.test(lowerPrompt) && /time|current\s*time|what\s*time/.test(lowerPrompt);
		if (asksNyTime) {
			try {
				const nowNy = new Date();
				const options = {
					timeZone: 'America/New_York',
					year: 'numeric',
					month: 'long',
					day: '2-digit',
					hour: '2-digit',
					minute: '2-digit',
					second: '2-digit',
					hour12: false
				};
				const formatted = new Intl.DateTimeFormat('en-US', options).format(nowNy);
				return res.json({
					reply: `Current time in New York (America/New_York): ${formatted}`,
					model: 'local-fallback',
					usage: null
				});
			} catch (e) {
				// continue to OpenAI if formatting fails
			}
		}

		const modelToUse = model || defaultModel;

		const requestBody = {
			model: modelToUse,
			messages: [{ role: 'user', content: prompt }],
		};
		if (typeof temperature === 'number') {
			requestBody.temperature = temperature;
		}
		if (typeof max_tokens === 'number') {
			requestBody.max_tokens = max_tokens;
		}

		const completion = await openai.chat.completions.create(requestBody);

		const replyText = completion?.choices?.[0]?.message?.content?.trim?.() || '';
		return res.json({ reply: replyText, model: modelToUse, usage: completion.usage });
	} catch (error) {
		const status = error?.status || 500;
		return res.status(status).json({
			error: 'OpenAI request failed',
			details: error?.error?.message || error?.message || 'Unknown error',
		});
	}
});

// Basic error handler
// eslint-disable-next-line no-unused-vars
app.use((err, req, res, next) => {
	const statusCode = err?.status || 500;
	return res.status(statusCode).json({ error: 'Server error', details: err?.message || 'Unknown error' });
});

const port = Number(process.env.PORT) || 3001;
app.listen(port, () => {
	// eslint-disable-next-line no-console
	console.log(`OpenAI Proxy listening on http://localhost:${port}`);
});


