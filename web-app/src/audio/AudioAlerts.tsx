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
import { postWebLog } from '../dataProvider';
import { getApiUrl, getApiKey } from '../lib/api-config';

const API_URL = getApiUrl();
const API_KEY = getApiKey();

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

// Helper to get saved setting with fallback
const getSavedSetting = (key: string, defaultValue: any): any => {
  const saved = localStorage.getItem(key);
  if (saved === null) return defaultValue;
  if (typeof defaultValue === 'number') return parseInt(saved) || defaultValue;
  if (typeof defaultValue === 'boolean') return saved === 'true';
  return saved;
};

export const AudioAlerts = () => {
  const [selectedDevice, setSelectedDevice] = useState('');
  const [devices, setDevices] = useState<any[]>([]);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [lastFileName, setLastFileName] = useState<string>(getSavedSetting('lastAudioFileName', ''));
  // const [libraryFiles, setLibraryFiles] = useState<any[]>([]); // Reserved for future library feature
  const [priority, setPriority] = useState(getSavedSetting('audioAlertPriority', 5));
  const [volume, setVolume] = useState(getSavedSetting('audioAlertVolume', 25));
  const [playCount, setPlayCount] = useState(getSavedSetting('audioAlertPlayCount', 1));
  const [interruptCurrent, setInterruptCurrent] = useState(getSavedSetting('audioAlertInterrupt', false));
  const [saveToFile, setSaveToFile] = useState(getSavedSetting('audioAlertSaveToFile', false));
  const [filename, setFilename] = useState('');
  const [loading, setLoading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState(0);
  
  const dataProvider = useDataProvider();
  const notify = useNotify();
  // const refresh = useRefresh(); // Unused, commented out

  // Load audio library on mount (disabled for now)
  const loadAudioLibrary = React.useCallback(async () => {
    // Reserved for future library feature
    // try {
    //   const response = await fetch(`${API_URL}/audio/library`, {
    //     headers: { 'X-API-Key': API_KEY },
    //   });
    //   if (response.ok) {
    //     const data = await response.json();
    //     setLibraryFiles(data.files || []);
    //   }
    // } catch (error) {
    //   console.error('Failed to load audio library:', error);
    // }
  }, []);

  // Load devices on mount and check for pre-selected device
  React.useEffect(() => {
    dataProvider.getList('devices', {
      filter: {}, // Show all devices, not just online ones
      pagination: { page: 1, perPage: 100 },
      sort: { field: 'id', order: 'ASC' },
    }).then(({ data }) => {
      setDevices(data); // Show all devices, let user decide which to send to
      
      // Check if a device was pre-selected from DeviceList
      const preselected = sessionStorage.getItem('preselectedDevice');
      if (preselected) {
        setSelectedDevice(preselected);
        sessionStorage.removeItem('preselectedDevice');
      }
    }).catch(() => {
      notify('Failed to load devices', { type: 'error' });
    });
    
    // Load audio library
    loadAudioLibrary();
  }, [dataProvider, notify, loadAudioLibrary]);

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
      setLastFileName(file.name);
      localStorage.setItem('lastAudioFileName', file.name);
      
      // Always update filename when saveToFile is enabled and selecting a new file
      if (saveToFile) {
        setFilename(file.name.replace(/\.[^/.]+$/, '.opus'));
      }
    }
  };

  // Save settings to localStorage when they change
  React.useEffect(() => {
    localStorage.setItem('audioAlertVolume', volume.toString());
  }, [volume]);

  React.useEffect(() => {
    localStorage.setItem('audioAlertPriority', priority.toString());
  }, [priority]);

  React.useEffect(() => {
    localStorage.setItem('audioAlertPlayCount', playCount.toString());
  }, [playCount]);

  React.useEffect(() => {
    localStorage.setItem('audioAlertInterrupt', interruptCurrent.toString());
  }, [interruptCurrent]);

  React.useEffect(() => {
    localStorage.setItem('audioAlertSaveToFile', saveToFile.toString());
  }, [saveToFile]);

  const handleSendFromLibrary = async (libraryFilename?: string) => {
    if (!selectedDevice) {
      notify('Please select a device', { type: 'error' });
      return;
    }

    const filenameToSend = libraryFilename || lastFileName;
    if (!filenameToSend) {
      notify('No file in library to send', { type: 'error' });
      return;
    }

    setLoading(true);
    try {
      const response = await fetch(`${API_URL}/audio/library/send`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'X-API-Key': API_KEY,
        },
        body: JSON.stringify({
          deviceId: selectedDevice,
          filename: filenameToSend.replace(/\.[^.]+$/, '.opus'), // Ensure .opus extension
          volume,
          priority,
          playCount,
          interruptCurrent,
        }),
      });

      if (!response.ok) {
        const error = await response.json();
        throw new Error(error.error || 'Failed to send');
      }

      notify('Audio sent from library', { type: 'success' });
    } catch (error: any) {
      notify(error.message || 'Failed to send from library', { type: 'error' });
    } finally {
      setLoading(false);
    }
  };

  const handleTestPing = async () => {
    if (!selectedDevice) {
      notify('Please select a device', { type: 'error' });
      return;
    }

    setLoading(true);
    try {
      const response = await fetch(`${API_URL}/audio/ping`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'X-API-Key': API_KEY,
        },
        body: JSON.stringify({ deviceId: selectedDevice }),
      });

      if (!response.ok) {
        const error = await response.json();
        throw new Error(error.error || 'Ping failed');
      }

      notify('Test ping sent successfully', { type: 'success' });
    } catch (error: any) {
      notify(error.message || 'Failed to send ping', { type: 'error' });
    } finally {
      setLoading(false);
    }
  };

  const handleSubmit = async () => {
    if (!selectedDevice) {
      notify('Please select a device', { type: 'error' });
      return;
    }
    
    if (!selectedFile) {
      notify('Please select an audio file first', { type: 'error' });
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
      // Log intent from web-app
      await postWebLog('info', 'Sending audio alert', {
        source: 'AudioAlerts',
        deviceId: selectedDevice,
        volume,
        priority,
        playCount,
        interruptCurrent,
        saveToFile,
        filename: saveToFile ? filename : undefined,
        originalName: selectedFile.name,
        sizeBytes: selectedFile.size,
      });

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
      // Log success from web-app
      await postWebLog('info', 'Audio alert sent successfully', {
        source: 'AudioAlerts',
        deviceId: selectedDevice,
        deviceName,
        volume,
        priority,
        playCount,
        interruptCurrent,
        saveToFile,
        filename: saveToFile ? filename : undefined,
        originalName: selectedFile.name,
        sizeBytes: selectedFile.size,
      });
      
      // Reload library to include newly uploaded file
      loadAudioLibrary();
      
      // Don't reset the selected file - keep it for potential re-use
      setUploadProgress(100);
      
    } catch (error: any) {
      notify(error.message || 'Failed to send audio alert', { type: 'error' });
      // Log failure from web-app
      await postWebLog('error', 'Audio alert failed', {
        source: 'AudioAlerts',
        deviceId: selectedDevice,
        volume,
        error: error?.message || String(error),
      });
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
                    <MenuItem key={device.id} value={device.deviceId}>
                      {device.clientId || device.deviceId} - {device.type} ({device.status || 'unknown'})
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
                  {selectedFile ? 'Change Audio File' : (lastFileName ? `Select Audio File (last: ${lastFileName})` : 'Select Audio File')}
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
                      onDelete={() => {
                        setSelectedFile(null);
                        // Also reset the file input when manually clearing
                        const fileInput = document.getElementById('audio-file-input') as HTMLInputElement;
                        if (fileInput) fileInput.value = '';
                      }}
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
                      onChange={(e) => {
                        setSaveToFile(e.target.checked);
                        if (e.target.checked) {
                          // Set default filename when enabling save to file
                          if (selectedFile) {
                            setFilename(selectedFile.name.replace(/\.[^/.]+$/, '.opus'));
                          }
                        } else {
                          // Clear filename when disabling save to file
                          setFilename('');
                        }
                      }}
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

              {/* Quick Actions */}
              <Stack direction="row" spacing={2} flexWrap="wrap">
                <Button
                  variant="outlined"
                  color="secondary"
                  onClick={handleTestPing}
                  disabled={loading || !selectedDevice}
                  startIcon={<PlayArrowIcon />}
                >
                  Test Ping
                </Button>
                
                {lastFileName && (
                  <Button
                    variant="contained"
                    color="primary"
                    onClick={() => handleSendFromLibrary()}
                    disabled={loading || !selectedDevice}
                    startIcon={<VolumeUpIcon />}
                  >
                    Send Last: {lastFileName.substring(0, 20)}{lastFileName.length > 20 ? '...' : ''}
                  </Button>
                )}
              </Stack>

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
