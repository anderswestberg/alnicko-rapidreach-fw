/**
 * Centralized API configuration
 * Auto-detects the correct API URL based on how the app is accessed
 */

export const getApiUrl = () => {
  // Use environment variable if set
  const envUrl = import.meta.env.VITE_API_URL;
  if (envUrl) {
    console.log('Using env API URL:', envUrl);
    return envUrl;
  }
  
  const protocol = window.location.protocol;
  const hostname = window.location.hostname;
  const currentPort = window.location.port;
  
  // If accessing via domain name (no port or standard ports), use same origin /api
  // Also use same origin for k3s ingress ports (8080/8443)
  if (!currentPort || currentPort === '80' || currentPort === '443' || 
      currentPort === '8080' || currentPort === '8443') {
    console.log('Auto-detected API URL (same origin):', '/api');
    return '/api';
  }
  
  // If accessing via k3s NodePort (30080), use device-server NodePort (30002)
  const port = currentPort === '30080' ? '30002' : '3002';
  
  const url = `${protocol}//${hostname}:${port}/api`;
  console.log('Auto-detected API URL:', url);
  return url;
};

export const getApiKey = () => {
  return import.meta.env.VITE_API_KEY || 'test-api-key';
};

// For components that need just the base URL without /api
export const getApiBaseUrl = () => {
  const apiUrl = getApiUrl();
  // If it's same origin, return empty string
  if (apiUrl === '/api') return '';
  // Otherwise remove the /api suffix
  return apiUrl.replace(/\/api$/, '');
};
