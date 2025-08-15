import { Router, Request, Response } from 'express';
import { DeviceMqttClient } from '../services/mqtt-client.js';
import logger from '../utils/logger.js';

// React-Admin expects these specific query parameters
// Currently unused but kept for future validation
// const listParamsSchema = z.object({
//   sort: z.string().optional(), // JSON array like ["title","ASC"]
//   range: z.string().optional(), // JSON array like [0, 24]
//   filter: z.string().optional(), // JSON object like {"title":"bar"}
// });

export function createDataProviderRoutes(mqttClient: DeviceMqttClient): Router {
  const router = Router();

  // Helper function to parse React-Admin query parameters
  const parseListParams = (query: any) => {
    const sort = query.sort ? JSON.parse(query.sort) : ['id', 'ASC'];
    const range = query.range ? JSON.parse(query.range) : [0, 9];
    const filter = query.filter ? JSON.parse(query.filter) : {};
    
    return { sort, range, filter };
  };

  // Helper function to apply filters
  const applyFilters = (items: any[], filter: any) => {
    if (!filter || Object.keys(filter).length === 0) {
      return items;
    }

    return items.filter(item => {
      return Object.entries(filter).every(([key, value]) => {
        // Handle array filters (for getMany)
        if (key === 'id' && Array.isArray(value)) {
          return value.includes(item.id);
        }
        
        // Handle search filters
        if (key === 'q' && typeof value === 'string') {
          const searchValue = value.toLowerCase();
          return (
            item.id?.toLowerCase().includes(searchValue) ||
            item.type?.toLowerCase().includes(searchValue) ||
            item.metadata?.ipAddress?.toLowerCase().includes(searchValue)
          );
        }
        
        // Handle exact matches
        if (item[key] !== undefined) {
          return item[key] === value;
        }
        
        // Handle nested properties (e.g., metadata.firmwareVersion)
        const keys = key.split('.');
        let current = item;
        for (const k of keys) {
          if (current && typeof current === 'object' && k in current) {
            current = current[k];
          } else {
            return false;
          }
        }
        return current === value;
      });
    });
  };

  // Helper function to apply sorting
  const applySort = (items: any[], sort: [string, 'ASC' | 'DESC']) => {
    const [field, order] = sort;
    
    return [...items].sort((a, b) => {
      // Handle nested properties
      const keys = field.split('.');
      let aValue = a;
      let bValue = b;
      
      for (const key of keys) {
        aValue = aValue?.[key];
        bValue = bValue?.[key];
      }
      
      if (aValue === undefined || bValue === undefined) {
        return 0;
      }
      
      const result = aValue < bValue ? -1 : aValue > bValue ? 1 : 0;
      return order === 'ASC' ? result : -result;
    });
  };

  // GET /dataprovider/:resource - getList
  router.get('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const { sort, range, filter } = parseListParams(req.query);
      
      // For now, we only support 'devices' resource
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      // Get all devices
      let items = mqttClient.getDevices();
      
      // Apply filters
      items = applyFilters(items, filter);
      
      // Apply sorting
      items = applySort(items, sort);
      
      // Get total count before pagination
      const total = items.length;
      
      // Apply pagination
      const [start, end] = range;
      const paginatedItems = items.slice(start, end + 1);
      
      // React-Admin expects the total count in the Content-Range header
      res.set('Content-Range', `${resource} ${start}-${end}/${total}`);
      res.set('X-Total-Count', total.toString());
      
      res.json(paginatedItems);
    } catch (error) {
      logger.error('Error in getList:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // GET /dataprovider/:resource/:id - getOne
  router.get('/dataprovider/:resource/:id', (req: Request, res: Response) => {
    try {
      const { resource, id } = req.params;
      
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      const device = mqttClient.getDevice(id);
      
      if (!device) {
        res.status(404).json({ error: 'Record not found' });
        return;
      }
      
      res.json(device);
    } catch (error) {
      logger.error('Error in getOne:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // POST /dataprovider/:resource - create
  router.post('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      // For devices, we can't really "create" them as they connect via MQTT
      // But we can return a success response for React-Admin compatibility
      const newDevice = {
        ...req.body,
        id: req.body.id || `device-${Date.now()}`,
        createdAt: new Date(),
      };
      
      res.status(201).json(newDevice);
    } catch (error) {
      logger.error('Error in create:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // PUT /dataprovider/:resource/:id - update
  router.put('/dataprovider/:resource/:id', (req: Request, res: Response) => {
    try {
      const { resource, id } = req.params;
      
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      const device = mqttClient.getDevice(id);
      
      if (!device) {
        res.status(404).json({ error: 'Record not found' });
        return;
      }
      
      // For devices, updates are limited as most data comes from MQTT
      // But we can simulate an update for React-Admin compatibility
      const updatedDevice = {
        ...device,
        ...req.body,
        id, // Ensure ID doesn't change
        updatedAt: new Date(),
      };
      
      res.json(updatedDevice);
    } catch (error) {
      logger.error('Error in update:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // DELETE /dataprovider/:resource/:id - delete
  router.delete('/dataprovider/:resource/:id', (req: Request, res: Response) => {
    try {
      const { resource, id } = req.params;
      
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      const device = mqttClient.getDevice(id);
      
      if (!device) {
        res.status(404).json({ error: 'Record not found' });
        return;
      }
      
      // For devices, we can't really delete them as they're managed by MQTT
      // But we can return the device for React-Admin compatibility
      res.json(device);
    } catch (error) {
      logger.error('Error in delete:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // GET /dataprovider/:resource?filter={id:[...]} - getMany
  // This uses the same endpoint as getList but with an id array filter
  
  // GET /dataprovider/:resource?filter={field:value} - getManyReference
  // This also uses the same endpoint as getList but with field filters

  // PUT /dataprovider/:resource - updateMany
  router.put('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const { ids } = req.body;
      
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      // Update multiple records
      const updatedIds = ids.filter((id: string) => mqttClient.getDevice(id));
      
      res.json({ data: updatedIds });
    } catch (error) {
      logger.error('Error in updateMany:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // DELETE /dataprovider/:resource?filter={id:[...]} - deleteMany
  router.delete('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const { filter } = parseListParams(req.query);
      
      if (resource !== 'devices') {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      
      const ids = filter.id || [];
      const deletedIds = ids.filter((id: string) => mqttClient.getDevice(id));
      
      res.json({ data: deletedIds });
    } catch (error) {
      logger.error('Error in deleteMany:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  return router;
}
