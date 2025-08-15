import { Request, Response, NextFunction } from 'express';
import config from '../config/index.js';
import logger from '../utils/logger.js';

export function authMiddleware(req: Request, res: Response, next: NextFunction): void {
  // Skip auth in development mode if no API key is configured
  if (config.server.nodeEnv === 'development' && !config.api.apiKey) {
    return next();
  }

  const apiKey = req.headers['x-api-key'] || req.query.apiKey;

  if (!apiKey) {
    res.status(401).json({
      success: false,
      error: 'API key required',
    });
    return;
  }

  if (apiKey !== config.api.apiKey) {
    logger.warn(`Invalid API key attempt from ${req.ip}`);
    res.status(403).json({
      success: false,
      error: 'Invalid API key',
    });
    return;
  }

  next();
}
