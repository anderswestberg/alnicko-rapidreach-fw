import { MongoClient } from 'mongodb';

async function cleanupOldLogs() {
  const mongoUri = process.env.MONGODB_URI || 'mongodb://localhost:27017';
  const mongoDb = process.env.MONGODB_DB || 'rapidreach';
  
  console.log('Connecting to MongoDB...');
  const client = new MongoClient(mongoUri);
  
  try {
    await client.connect();
    const db = client.db(mongoDb);
    const collection = db.collection('logs');
    
    // Count old format logs
    const oldFormatCount = await collection.countDocuments({
      message: { $regex: '^{"source"' }
    });
    
    console.log(`Found ${oldFormatCount} logs in old format`);
    
    if (oldFormatCount > 0) {
      // Delete old format logs
      const result = await collection.deleteMany({
        message: { $regex: '^{"source"' }
      });
      
      console.log(`Deleted ${result.deletedCount} old format logs`);
    }
    
    // Show sample of remaining logs
    const sampleLogs = await collection.find({ module: { $exists: true } })
      .sort({ _id: -1 })
      .limit(5)
      .toArray();
    
    console.log('\nSample of properly formatted logs:');
    sampleLogs.forEach(log => {
      console.log(`[${log.level}] ${log.module}: ${log.message}`);
    });
    
  } catch (error) {
    console.error('Error:', error);
  } finally {
    await client.close();
  }
}

cleanupOldLogs();
