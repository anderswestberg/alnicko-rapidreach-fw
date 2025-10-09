import { Router, Request, Response } from 'express';
import { DeviceMqttClient } from '../services/mqtt-client.js';
import logger from '../utils/logger.js';
import { getCollection } from '../db/mongo.js';
import { ObjectId } from 'mongodb';

// React-Admin expects these specific query parameters
// Currently unused but kept for future validation
// const listParamsSchema = z.object({
//   sort: z.string().optional(), // JSON array like ["title","ASC"]
//   range: z.string().optional(), // JSON array like [0, 24]
//   filter: z.string().optional(), // JSON object like {"title":"bar"}
// });

export function createDataProviderRoutes(_mqttClient: DeviceMqttClient): Router {
  const router = Router();

  // Helper function to parse React-Admin query parameters
  const parseListParams = (query: any) => {
    const sort = query.sort ? JSON.parse(query.sort) : ['id', 'ASC'];
    const range = query.range ? JSON.parse(query.range) : [0, 9];
    const filter = query.filter ? JSON.parse(query.filter) : {};
    
    return { sort, range, filter };
  };

  // In-memory helpers removed; Mongo handles filtering/sorting

  // GET /dataprovider/:resource - getList (Mongo-backed for generic resources)
  router.get('/dataprovider/:resource', async (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const { sort, range, filter } = parseListParams(req.query);

      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }

      const col = getCollection(resource);
      const [field, order] = sort;
      const [start, end] = range;
      const limit = Math.max(0, end - start + 1);

      // Build Mongo query
      const mongoFilter: any = filter || {};
      // If filter.id is array, translate to { id: { $in: [...] } }
      if (Array.isArray(mongoFilter.id)) {
        mongoFilter.id = { $in: mongoFilter.id };
      }

      const total = await col.countDocuments(mongoFilter);
      
      // If sorting by level, sort by levelNo instead for efficiency
      const sortField = (field === 'level' && resource === 'logs') ? 'levelNo' : field;
      const sortSpec: Record<string, 1 | -1> = { [sortField]: order === 'ASC' ? 1 : -1 };
      
      const cursor = col
        .find(mongoFilter)
        .sort(sortSpec)
        .skip(start)
        .limit(limit);
      const items = await cursor.toArray();
      
      // Transform _id to id for React-Admin (always use MongoDB _id as the id)
      const transformedItems = items.map(item => ({
        ...item,
        id: item._id?.toString() || item.id,
      }));

      // Return data in React-Admin expected format
      res.json({
        data: transformedItems,
        total: total
      });
    } catch (error) {
      logger.error('Error in getList:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // GET /dataprovider/:resource/:id - getOne (Mongo-backed)
  router.get('/dataprovider/:resource/:id', (req: Request, res: Response) => {
    try {
      const { resource, id } = req.params;

      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }

      const col = getCollection(resource);
      
      // Try to parse as ObjectId if it looks like one
      let query: any;
      try {
        if (ObjectId.isValid(id) && id.length === 24) {
          query = { $or: [{ _id: new ObjectId(id) }, { _id: id }, { deviceId: id }, { id }] };
        } else {
          // For devices, prefer deviceId lookup; for others use id
          if (resource === 'devices') {
            query = { $or: [{ deviceId: id }, { clientId: id }, { id }, { _id: id }] };
          } else {
            query = { $or: [{ id }, { _id: id }] };
          }
        }
      } catch (e) {
        query = { $or: [{ id }, { _id: id }, { deviceId: id }] };
      }
      
      col.findOne(query).then((doc) => {
        if (!doc) {
          res.status(404).json({ error: 'Record not found' });
          return;
        }
        // Transform _id to id for React-Admin (always use MongoDB _id)
        const transformed = {
          ...doc,
          id: doc._id?.toString() || doc.id,
        };
        res.json(transformed);
      }).catch((err) => {
        logger.error('Error in getOne:', err);
        res.status(500).json({ error: 'Internal server error' });
      });
    } catch (error) {
      logger.error('Error in getOne:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // POST /dataprovider/:resource - create (Mongo-backed)
  router.post('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      const col = getCollection(resource);
      const doc = { ...req.body };
      if (!doc.id) doc.id = `${resource}-${Date.now()}`;
      col.insertOne(doc).then(() => res.status(201).json(doc)).catch(err => {
        logger.error('Error in create:', err);
        res.status(500).json({ error: 'Internal server error' });
      });
    } catch (error) {
      logger.error('Error in create:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // PUT /dataprovider/:resource/:id - update (Mongo-backed)
  router.put('/dataprovider/:resource/:id', (req: Request, res: Response) => {
    try {
      const { resource, id } = req.params;
      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      const col = getCollection(resource);
      col.findOneAndUpdate(
        { id },
        { $set: { ...req.body, id, updatedAt: new Date() } },
        { returnDocument: 'after' }
      ).then(result => {
        if (!result.value) {
          res.status(404).json({ error: 'Record not found' });
          return;
        }
        res.json(result.value);
      }).catch(err => {
        logger.error('Error in update:', err);
        res.status(500).json({ error: 'Internal server error' });
      });
    } catch (error) {
      logger.error('Error in update:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // DELETE /dataprovider/:resource/:id - delete (Mongo-backed)
  router.delete('/dataprovider/:resource/:id', (req: Request, res: Response) => {
    try {
      const { resource, id } = req.params;
      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      const col = getCollection(resource);
      col.findOneAndDelete({ id }).then(result => {
        if (!result.value) {
          res.status(404).json({ error: 'Record not found' });
          return;
        }
        res.json(result.value);
      }).catch(err => {
        logger.error('Error in delete:', err);
        res.status(500).json({ error: 'Internal server error' });
      });
    } catch (error) {
      logger.error('Error in delete:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // GET /dataprovider/:resource?filter={id:[...]} - getMany
  // This uses the same endpoint as getList but with an id array filter
  
  // GET /dataprovider/:resource?filter={field:value} - getManyReference
  // This also uses the same endpoint as getList but with field filters

  // PUT /dataprovider/:resource - updateMany (Mongo-backed)
  router.put('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const { ids, data } = req.body;
      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      const col = getCollection(resource);
      col.updateMany({ id: { $in: ids } }, { $set: data }).then(() => {
        res.json({ data: ids });
      }).catch((err) => {
        logger.error('Error in updateMany:', err);
        res.status(500).json({ error: 'Internal server error' });
      });
    } catch (error) {
      logger.error('Error in updateMany:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  // DELETE /dataprovider/:resource?filter={id:[...]} - deleteMany (Mongo-backed)
  router.delete('/dataprovider/:resource', (req: Request, res: Response) => {
    try {
      const { resource } = req.params;
      const { filter } = parseListParams(req.query);
      const allowed = ['devices', 'logs'];
      if (!allowed.includes(resource)) {
        res.status(404).json({ error: 'Resource not found' });
        return;
      }
      const ids = Array.isArray(filter?.id) ? filter.id : [];
      const col = getCollection(resource);
      col.deleteMany({ id: { $in: ids } }).then(() => {
        res.json({ data: ids });
      }).catch((err) => {
        logger.error('Error in deleteMany:', err);
        res.status(500).json({ error: 'Internal server error' });
      });
    } catch (error) {
      logger.error('Error in deleteMany:', error);
      res.status(500).json({ error: 'Internal server error' });
    }
  });

  return router;
}
