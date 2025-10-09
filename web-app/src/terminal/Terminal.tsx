import { useEffect } from 'react';
import { Card, CardContent, Typography, Alert, Box, Link } from '@mui/material';
import { Title } from 'react-admin';

export const Terminal = () => {
  // Get deviceId from URL hash params if any
  const hash = window.location.hash;
  const searchParams = new URLSearchParams(hash.split('?')[1] || '');
  const deviceId = searchParams.get('deviceId');

  useEffect(() => {
    // If there's a standalone terminal app, we could iframe it here
    // For now, just show instructions
  }, [deviceId]);

  return (
    <>
      <Title title="Device Terminal" />
      <Card>
        <CardContent>
          <Typography variant="h5" gutterBottom>
            Device Terminal
          </Typography>
          
          {deviceId && (
            <Alert severity="info" sx={{ mb: 2 }}>
              Selected Device: <strong>{deviceId}</strong>
            </Alert>
          )}
          
          <Box sx={{ mt: 3 }}>
            <Typography variant="body1" paragraph>
              The MQTT terminal is available as a standalone application.
            </Typography>
            
            <Typography variant="body2" color="text.secondary" paragraph>
              To use the terminal:
            </Typography>
            
            <Typography component="div" variant="body2" color="text.secondary">
              <ul>
                <li>Open the mqtt-terminal app (usually running on a separate port)</li>
                <li>Or use the shell via MQTT by publishing to: <code>devices/{deviceId}-shell/rx</code></li>
                <li>Subscribe to responses on: <code>devices/{deviceId}-shell/tx</code></li>
              </ul>
            </Typography>
            
            {deviceId && (
              <Alert severity="success" sx={{ mt: 2 }}>
                <Typography variant="body2">
                  You can also send shell commands directly via MQTT:
                </Typography>
                <Box component="pre" sx={{ mt: 1, p: 1, bgcolor: 'grey.100', borderRadius: 1, fontSize: '0.875rem' }}>
{`mosquitto_pub -h 192.168.2.79 \\
  -t "devices/${deviceId}/rx" \\
  -m "help"`}
                </Box>
              </Alert>
            )}
            
            <Box sx={{ mt: 3 }}>
              <Link 
                href="https://github.com/yourusername/mqtt-terminal" 
                target="_blank"
                rel="noopener noreferrer"
              >
                View MQTT Terminal Documentation →
              </Link>
            </Box>
          </Box>
        </CardContent>
      </Card>
    </>
  );
};

