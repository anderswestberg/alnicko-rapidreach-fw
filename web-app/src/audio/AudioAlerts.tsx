import React, { useState } from 'react';
import {
  Box,
  Button,
  Card,
  CardContent,
  FormControl,
  InputLabel,
  MenuItem,
  Select,
  TextField,
  Typography,
  Alert,
  LinearProgress,
  Slider,
  FormControlLabel,
  Checkbox,
  Stack,
  Chip,
} from '@mui/material';
import { useDataProvider, useNotify, Title } from 'react-admin';
import CloudUploadIcon from '@mui/icons-material/CloudUpload';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import { styled } from '@mui/material/styles';

const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:3000/api';
const API_KEY = import.meta.env.VITE_API_KEY || 'test-api-key';

const VisuallyHiddenInput = styled('input')({
  clip: 'rect(0 0 0 0)',
  clipPath: 'inset(50%)',
  height: 1,
  overflow: 'hidden',
  position: 'absolute',
  bottom: 0,
  left: 0,
  whiteSpace: 'nowrap',
  width: 1,
});

export const AudioAlerts = () => {
  const [selectedDevice, setSelectedDevice] = useState('');
  const [devices, setDevices] = useState<any[]>([]);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [priority, setPriority] = useState(5);
  const [volume, setVolume] = useState(25);
  const [playCount, setPlayCount] = useState(1);
  const [interruptCurrent, setInterruptCurrent] = useState(false);
  const [saveToFile, setSaveToFile] = useState(false);
  const [filename, setFilename] = useState('');
  const [loading, setLoading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState(0);
  
  const dataProvider = useDataProvider();
  const notify = useNotify();
  // const refresh = useRefresh(); // Unused, commented out

  // Load devices on mount
  React.useEffect(() => {
    dataProvider.getList('devices', {
      filter: {}, // Show all devices, not just online ones
      pagination: { page: 1, perPage: 100 },
      sort: { field: 'id', order: 'ASC' },
    }).then(({ data }) => {
      setDevices(data); // Show all devices, let user decide which to send to
    }).catch(() => {
      notify('Failed to load devices', { type: 'error' });
    });
  }, [dataProvider, notify]);

  const handleFileSelect = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (file) {
      // Validate file type
      const allowedTypes = ['audio/mpeg', 'audio/mp3', 'audio/wav', 'audio/ogg', 'audio/opus', 'audio/flac'];
      if (!allowedTypes.some(type => file.type.includes(type))) {
        notify('Invalid file type. Please select an audio file.', { type: 'error' });
        return;
      }
      
      // Validate file size (10MB max)
      if (file.size > 10 * 1024 * 1024) {
        notify('File too large. Maximum size is 10MB.', { type: 'error' });
        return;
      }
      
      setSelectedFile(file);
      if (saveToFile && !filename) {
        setFilename(file.name.replace(/\.[^/.]+$/, '.opus'));
      }
    }
  };

  const handleSubmit = async () => {
    if (!selectedDevice || !selectedFile) {
      notify('Please select a device and audio file', { type: 'error' });
      return;
    }

    setLoading(true);
    setUploadProgress(0);

    const formData = new FormData();
    formData.append('audio', selectedFile);
    
    // Send metadata as JSON to preserve types
    const metadata = {
      deviceId: selectedDevice,
      priority: priority,
      volume: volume,
      playCount: playCount,
      interruptCurrent: interruptCurrent,
      saveToFile: saveToFile,
      ...(saveToFile && filename ? { filename } : {})
    };
    formData.append('metadata', JSON.stringify(metadata));

    try {
      // Get auth token
      // const token = localStorage.getItem('auth_token'); // Unused, commented out
      
      const response = await fetch(`${API_URL}/audio/alert`, {
        method: 'POST',
        headers: {
          'X-API-Key': API_KEY,
        },
        body: formData,
      });

      if (!response.ok) {
        const error = await response.json();
        throw new Error(error.error || 'Upload failed');
      }

      // const result = await response.json(); // Unused, commented out
      const deviceName = devices.find(d => (d.clientId || d.id) === selectedDevice)?.clientId || selectedDevice;
      notify(`Audio alert sent to ${deviceName}`, { type: 'success' });
      
      // Reset form
      setSelectedFile(null);
      setUploadProgress(100);
      
      // Reset file input
      const fileInput = document.getElementById('audio-file-input') as HTMLInputElement;
      if (fileInput) fileInput.value = '';
      
    } catch (error: any) {
      notify(error.message || 'Failed to send audio alert', { type: 'error' });
    } finally {
      setLoading(false);
      setTimeout(() => setUploadProgress(0), 1000);
    }
  };

  return (
    <>
      <Title title="Audio Alerts" />
      <Card>
        <CardContent>
          <Typography variant="h5" gutterBottom>
            Send Audio Alert to Device
          </Typography>
          
          <Box sx={{ mt: 3 }}>
            <Stack spacing={3}>
              {/* Device Selection */}
              <FormControl fullWidth>
                <InputLabel>Select Device</InputLabel>
                <Select
                  value={selectedDevice}
                  onChange={(e) => setSelectedDevice(e.target.value)}
                  label="Select Device"
                >
                  {devices.map((device) => (
                    <MenuItem key={device.id} value={device.clientId || device.id}>
                      {device.clientId || device.id} - {device.type} ({device.status || 'unknown'})
                    </MenuItem>
                  ))}
                </Select>
              </FormControl>

              {/* File Upload */}
              <Box>
                <Button
                  component="label"
                  variant="contained"
                  startIcon={<CloudUploadIcon />}
                  disabled={loading}
                >
                  Select Audio File
                  <VisuallyHiddenInput 
                    id="audio-file-input"
                    type="file" 
                    onChange={handleFileSelect}
                    accept="audio/*"
                  />
                </Button>
                {selectedFile && (
                  <Box sx={{ mt: 1 }}>
                    <Chip 
                      label={`${selectedFile.name} (${(selectedFile.size / 1024 / 1024).toFixed(2)} MB)`}
                      onDelete={() => setSelectedFile(null)}
                      color="primary"
                    />
                  </Box>
                )}
              </Box>

              {/* Priority */}
              <Box>
                <Typography gutterBottom>Priority: {priority}</Typography>
                <Slider
                  value={priority}
                  onChange={(_, value) => setPriority(value as number)}
                  min={0}
                  max={255}
                  marks={[
                    { value: 0, label: 'Low' },
                    { value: 128, label: 'Medium' },
                    { value: 255, label: 'High' },
                  ]}
                />
              </Box>

              {/* Volume */}
              <Box>
                <Typography gutterBottom>
                  <VolumeUpIcon sx={{ verticalAlign: 'middle', mr: 1 }} />
                  Volume: {volume}%
                </Typography>
                <Slider
                  value={volume}
                  onChange={(_, value) => setVolume(value as number)}
                  min={0}
                  max={100}
                  marks={[
                    { value: 0, label: '0%' },
                    { value: 50, label: '50%' },
                    { value: 100, label: '100%' },
                  ]}
                />
              </Box>

              {/* Play Count */}
              <TextField
                label="Play Count (0 = infinite)"
                type="number"
                value={playCount}
                onChange={(e) => setPlayCount(parseInt(e.target.value) || 0)}
                inputProps={{ min: 0, max: 100 }}
                fullWidth
              />

              {/* Options */}
              <Box>
                <FormControlLabel
                  control={
                    <Checkbox
                      checked={interruptCurrent}
                      onChange={(e) => setInterruptCurrent(e.target.checked)}
                    />
                  }
                  label="Interrupt current playback"
                />
                
                <FormControlLabel
                  control={
                    <Checkbox
                      checked={saveToFile}
                      onChange={(e) => setSaveToFile(e.target.checked)}
                    />
                  }
                  label="Save to device storage"
                />
              </Box>

              {/* Filename (if saving) */}
              {saveToFile && (
                <TextField
                  label="Filename on device"
                  value={filename}
                  onChange={(e) => setFilename(e.target.value)}
                  placeholder="alert.opus"
                  fullWidth
                />
              )}

              {/* Upload Progress */}
              {loading && (
                <Box>
                  <Typography variant="body2" color="text.secondary">
                    Processing audio...
                  </Typography>
                  <LinearProgress variant="determinate" value={uploadProgress} />
                </Box>
              )}

              {/* Submit Button */}
              <Button
                variant="contained"
                color="primary"
                size="large"
                onClick={handleSubmit}
                disabled={!selectedDevice || !selectedFile || loading}
                startIcon={<PlayArrowIcon />}
              >
                Send Audio Alert
              </Button>

              {/* Info */}
              <Alert severity="info">
                The audio file will be automatically converted to Opus format for optimal transmission.
                Supported formats: MP3, WAV, OGG, FLAC, AAC, M4A
              </Alert>
            </Stack>
          </Box>
        </CardContent>
      </Card>
    </>
  );
};
