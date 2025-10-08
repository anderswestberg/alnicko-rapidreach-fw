import type { DataProvider } from 'react-admin';
import axios from 'axios';

// Auto-detect API URL based on current hostname
const getApiUrl = () => {
  if (import.meta.env.VITE_API_URL) {
    return import.meta.env.VITE_API_URL;
  }
  
  // Use same hostname as web app, but port 3002
  const protocol = window.location.protocol;
  const hostname = window.location.hostname;
  return `${protocol}//${hostname}:3002/api`;
};

const API_URL = getApiUrl();
const API_KEY = import.meta.env.VITE_API_KEY || 'test-api-key';

const apiClient = axios.create({
  baseURL: API_URL,
  headers: {
    'X-API-Key': API_KEY,
  },
});

// Simple client-side logger that posts logs to the device-server
export async function postWebLog(level: 'info' | 'warn' | 'error', message: string, meta?: Record<string, any>) {
  try {
    await apiClient.post('/logs', {
      level,
      message,
      device: 'web-app', // origin component
      module: meta?.module || meta?.source || 'web-app', // normalize to module
      ...meta,
    });
  } catch (e) {
    // Avoid throwing from logger; fallback to console
    // eslint-disable-next-line no-console
    console.warn('web log post failed', e);
  }
}

// Generic data provider that works with the new data provider API
export const dataProvider: DataProvider = {
  getList: async (resource: string, params: any) => {
    const { page = 1, perPage = 10 } = params.pagination || {};
    const { field = 'id', order = 'ASC' } = params.sort || {};
    const { filter } = params;

    const query = new URLSearchParams();
    
    // Add sort parameter
    query.append('sort', JSON.stringify([field, order]));
    
    // Add range parameter (React-Admin uses 0-based indexing)
    const start = (page - 1) * perPage;
    const end = start + perPage - 1;
    query.append('range', JSON.stringify([start, end]));
    
    // Add filter parameter
    if (filter && Object.keys(filter).length > 0) {
      query.append('filter', JSON.stringify(filter));
    }

    try {
      const response = await apiClient.get(`/dataprovider/${resource}?${query.toString()}`);
      
      // The server should return { data: [...], total: number }
      // If it returns an array directly, wrap it
      const result = Array.isArray(response.data) 
        ? {
            data: response.data,
            // Try to get total from headers as fallback
            total: response.headers['x-total-count'] 
              ? parseInt(response.headers['x-total-count'])
              : response.data.length
          }
        : response.data;
      
      // Debug logging for pagination
      console.log('Pagination debug:', {
        resource,
        page: params.pagination?.page,
        perPage: params.pagination?.perPage,
        responseData: Array.isArray(response.data) ? 'array' : 'object',
        total: result.total,
        dataLength: result.data?.length
      });

      return result;
    } catch (error) {
      console.error('DataProvider error:', error);
      throw error;
    }
  },

  getOne: async (resource: string, params: any) => {
    const response = await apiClient.get(`/dataprovider/${resource}/${params.id}`);
    return { data: response.data };
  },

  getMany: async (resource: string, params: any) => {
    // Use getList with an id filter
    const query = new URLSearchParams();
    query.append('filter', JSON.stringify({ id: params.ids }));
    
    const response = await apiClient.get(`/dataprovider/${resource}?${query.toString()}`);
    return { data: response.data };
  },

  getManyReference: async (resource: string, params: any) => {
    const { page = 1, perPage = 10 } = params.pagination || {};
    const { field = 'id', order = 'ASC' } = params.sort || {};
    const filter = { ...params.filter, [params.target]: params.id };

    const query = new URLSearchParams();
    query.append('sort', JSON.stringify([field, order]));
    
    const start = (page - 1) * perPage;
    const end = start + perPage - 1;
    query.append('range', JSON.stringify([start, end]));
    query.append('filter', JSON.stringify(filter));

    const response = await apiClient.get(`/dataprovider/${resource}?${query.toString()}`);
    
    // The server should return { data: [...], total: number }
    // If it returns an array directly, wrap it
    const result = Array.isArray(response.data) 
      ? {
          data: response.data,
          total: response.headers['x-total-count'] 
            ? parseInt(response.headers['x-total-count'])
            : response.data.length
        }
      : response.data;

    return result;
  },

  create: async (resource: string, params: any) => {
    const response = await apiClient.post(`/dataprovider/${resource}`, params.data);
    return { data: response.data };
  },

  update: async (resource: string, params: any) => {
    // Special handling for device commands
    if (resource === 'devices' && params.data.command) {
      const response = await apiClient.post(
        `/devices/${params.id}/execute`,
        { command: params.data.command }
      );
      return { data: { ...params.data, output: response.data.output } };
    }
    
    // Generic update
    const response = await apiClient.put(`/dataprovider/${resource}/${params.id}`, params.data);
    return { data: response.data };
  },

  updateMany: async (resource: string, params: any) => {
    const response = await apiClient.put(`/dataprovider/${resource}`, {
      ids: params.ids,
      data: params.data,
    });
    return { data: response.data.data || params.ids };
  },

  delete: async (resource: string, params: any) => {
    const response = await apiClient.delete(`/dataprovider/${resource}/${params.id}`);
    return { data: response.data };
  },

  deleteMany: async (resource: string, params: any) => {
    const query = new URLSearchParams();
    query.append('filter', JSON.stringify({ id: params.ids }));
    
    const response = await apiClient.delete(`/dataprovider/${resource}?${query.toString()}`);
    return { data: response.data.data || params.ids };
  },
};

// Helper function to get device stats (using the original endpoint)
export const getDeviceStats = async () => {
  const { data } = await apiClient.get('/devices/stats');
  return data.stats;
};