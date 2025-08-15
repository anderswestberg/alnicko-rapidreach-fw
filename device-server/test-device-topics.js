import mqtt from 'mqtt';

const client = mqtt.connect('mqtt://localhost:1883', {
  username: 'admin',
  password: 'public',
  clientId: 'device-topic-test-' + Date.now(),
});

console.log('Connecting to MQTT broker...');

client.on('connect', () => {
  console.log('Connected!');
  
  // Test patterns for device topics
  const testPatterns = [
    '+_tx',             // This will fail due to mqtt.js validation
    '313938_tx',        // Exact topic
    '+/rx',             // Alternative pattern with /
    '+/tx',             // Alternative pattern with /
    'device/+/tx',      // With prefix
    'rapidreach/+/shell/out',  // Already works
  ];
  
  console.log('\nTesting device topic patterns:');
  
  testPatterns.forEach(pattern => {
    try {
      client.subscribe(pattern, { qos: 1 }, (err, granted) => {
        if (err) {
          console.log(`❌ Error subscribing to '${pattern}':`, err.message);
        } else if (granted && granted[0].qos === 128) {
          console.log(`❌ Subscription to '${pattern}' was rejected by broker (QoS 128)`);
        } else {
          console.log(`✅ Successfully subscribed to '${pattern}' with QoS ${granted[0].qos}`);
        }
      });
    } catch (e) {
      console.log(`❌ Client validation error for '${pattern}':`, e.message);
    }
  });
  
  // Wait a bit then test publishing to device-like topics
  setTimeout(() => {
    console.log('\nPublishing test messages:');
    
    const testMessages = [
      { topic: '313938_tx', message: 'Device response' },
      { topic: 'device/313938/tx', message: 'Alternative format' },
      { topic: '123456/tx', message: 'Another device' },
      { topic: 'rapidreach/313938/shell/out', message: 'Shell output' },
    ];
    
    testMessages.forEach(({ topic, message }) => {
      console.log(`Publishing to '${topic}': ${message}`);
      client.publish(topic, message);
    });
    
    // Give time for messages to arrive
    setTimeout(() => {
      console.log('\nTest complete. Press Ctrl+C to exit.');
    }, 2000);
  }, 1000);
});

client.on('message', (topic, message) => {
  console.log(`📨 Received: [${topic}] ${message.toString()}`);
});

client.on('error', (err) => {
  console.error('MQTT Error:', err);
});
