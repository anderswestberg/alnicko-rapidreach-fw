import type { DataProvider, GetListParams, GetListResult } from 'react-admin';
import axios from 'axios';

const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:3002/api';
const API_KEY = import.meta.env.VITE_API_KEY || 'your-secure-api-key-here';

const apiClient = axios.create({
  baseURL: API_URL,
  headers: {
    'X-API-Key': API_KEY,
  },
});

// Custom data provider for our device server
export const dataProvider: DataProvider = {
  getList: async (resource: string, params: GetListParams): Promise<GetListResult> => {
    if (resource === 'devices') {
      const { data } = await apiClient.get('/devices');
      
      // Apply pagination
      const { page, perPage } = params.pagination;
      const start = (page - 1) * perPage;
      const end = page * perPage;
      
      // Apply sorting if needed
      let devices = data.devices;
      if (params.sort) {
        const { field, order } = params.sort;
        devices = [...devices].sort((a, b) => {
          if (order === 'ASC') {
            return a[field] > b[field] ? 1 : -1;
          }
          return a[field] < b[field] ? 1 : -1;
        });
      }
      
      // Apply filters if needed
      if (params.filter && Object.keys(params.filter).length > 0) {
        devices = devices.filter((device: any) => {
          return Object.entries(params.filter).every(([key, value]) => {
            if (key === 'status' && value) {
              return device.status === value;
            }
            if (key === 'type' && value) {
              return device.type === value;
            }
            return true;
          });
        });
      }
      
      const paginatedDevices = devices.slice(start, end);
      
      return {
        data: paginatedDevices,
        total: devices.length,
      };
    }
    
    throw new Error(`Unknown resource: ${resource}`);
  },

  getOne: async (resource: string, params: any) => {
    if (resource === 'devices') {
      const { data } = await apiClient.get(`/devices/${params.id}`);
      return { data: data.device };
    }
    throw new Error(`Unknown resource: ${resource}`);
  },

  getMany: async (resource: string, params: any) => {
    if (resource === 'devices') {
      const { data } = await apiClient.get('/devices');
      const devices = data.devices.filter((device: any) => 
        params.ids.includes(device.id)
      );
      return { data: devices };
    }
    throw new Error(`Unknown resource: ${resource}`);
  },

  getManyReference: async () => {
    throw new Error('Not implemented');
  },

  create: async () => {
    throw new Error('Not implemented');
  },

  update: async (resource: string, params: any) => {
    if (resource === 'devices' && params.data.command) {
      // Execute command on device
      const { data } = await apiClient.post(
        `/devices/${params.id}/execute`,
        { command: params.data.command }
      );
      return { data: { ...params.data, output: data.output } };
    }
    throw new Error('Not implemented');
  },

  updateMany: async () => {
    throw new Error('Not implemented');
  },

  delete: async () => {
    throw new Error('Not implemented');
  },

  deleteMany: async () => {
    throw new Error('Not implemented');
  },
};

// Helper function to get device stats
export const getDeviceStats = async () => {
  const { data } = await apiClient.get('/devices/stats');
  return data.stats;
};
