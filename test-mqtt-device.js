#!/usr/bin/env node
import mqtt from 'mqtt';

const client = mqtt.connect('mqtt://localhost:1883', {
  username: 'admin',
  password: 'public',
  clientId: 'test-client-' + Date.now(),
});

console.log('Connecting to MQTT broker...');

client.on('connect', () => {
  console.log('✅ Connected to MQTT broker');
  
  // Subscribe to the new hierarchical topic format
  const deviceId = '313938';
  const rxTopic = `devices/${deviceId}/rx`;
  const txTopic = `devices/${deviceId}/tx`;
  
  console.log(`Subscribing to ${txTopic}...`);
  
  client.subscribe(txTopic, { qos: 1 }, (err, granted) => {
    if (err) {
      console.log(`❌ Error subscribing:`, err.message);
    } else if (granted && granted[0].qos === 128) {
      console.log(`❌ Subscription rejected by broker`);
    } else {
      console.log(`✅ Subscribed to ${txTopic} with QoS ${granted[0].qos}`);
      
      // Send a test command
      console.log(`\nSending command to ${rxTopic}...`);
      client.publish(rxTopic, 'help\n', { qos: 1 }, (err) => {
        if (err) {
          console.log('❌ Publish error:', err);
        } else {
          console.log('✅ Command sent');
        }
      });
    }
  });
});

client.on('message', (topic, message) => {
  console.log(`\n📨 Received from [${topic}]:`);
  console.log(message.toString());
});

client.on('error', (err) => {
  console.error('❌ MQTT Error:', err);
});

// Exit after 10 seconds
setTimeout(() => {
  console.log('\n⏱️ Timeout - exiting');
  client.end();
  process.exit(0);
}, 10000);



