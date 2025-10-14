import React, { useState, useEffect, useRef } from 'react';
import {
  Button,
  Card,
  CardContent,
  FormControl,
  InputLabel,
  MenuItem,
  Select,
  TextField,
  Typography,
  Paper,
  Stack,
  Chip,
} from '@mui/material';
import { useDataProvider, Title } from 'react-admin';
import SendIcon from '@mui/icons-material/Send';
import ClearIcon from '@mui/icons-material/Clear';
import { getApiUrl, getApiKey } from '../lib/api-config';

const API_URL = getApiUrl();
const API_KEY = getApiKey();

export const DeviceTerminal = () => {
  const [selectedDevice, setSelectedDevice] = useState('');
  const [devices, setDevices] = useState<any[]>([]);
  const [command, setCommand] = useState('');
  const [output, setOutput] = useState<string[]>([]);
  const [commandList, setCommandList] = useState<string[]>([]);
  const [historyIndex, setHistoryIndex] = useState(-1);
  const [loading, setLoading] = useState(false);
  const outputRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  
  const dataProvider = useDataProvider();

  // Load command history from localStorage on mount
  useEffect(() => {
    const savedHistory = localStorage.getItem('terminalCommandHistory');
    if (savedHistory) {
      try {
        const parsed = JSON.parse(savedHistory);
        setCommandList(parsed);
      } catch (e) {
        console.error('Failed to parse command history:', e);
      }
    }
  }, []);

  // Save command history to localStorage when it changes
  useEffect(() => {
    if (commandList.length > 0) {
      localStorage.setItem('terminalCommandHistory', JSON.stringify(commandList));
    }
  }, [commandList]);

  // Load devices and check for preselected
  useEffect(() => {
    dataProvider.getList('devices', {
      filter: {},
      pagination: { page: 1, perPage: 100 },
      sort: { field: 'id', order: 'ASC' },
    }).then(({ data }) => {
      setDevices(data);
      
      // Check for preselected device
      const preselected = sessionStorage.getItem('preselectedDevice');
      if (preselected) {
        setSelectedDevice(preselected);
        sessionStorage.removeItem('preselectedDevice');
        // Find the device to get its friendly name
        const device = data.find((d: any) => d.deviceId === preselected);
        const displayName = device ? (device.clientId || device.deviceId) : preselected;
        setOutput([`Connected to device: ${displayName}`, '']);
      }
      
      // Focus input after devices load
      setTimeout(() => inputRef.current?.focus(), 200);
    });
  }, [dataProvider]);

  // Auto-scroll output
  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output]);

  const handleSendCommand = async () => {
    if (!selectedDevice || !command.trim()) {
      return;
    }

    setLoading(true);
    const commandToSend = command.trim();
    setCommand('');
    
    // Add command to output
    setOutput(prev => [...prev, `$ ${commandToSend}`, '']);

    try {
      // Use the same endpoint as DeviceShow - selectedDevice is now the deviceId
      const response = await fetch(`${API_URL}/devices/${encodeURIComponent(selectedDevice)}/execute`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'X-API-Key': API_KEY,
        },
        body: JSON.stringify({
          command: commandToSend,
        }),
      });

      if (!response.ok) {
        const error = await response.json();
        throw new Error(error.error || 'Failed to send command');
      }

      const result = await response.json();
      
      // Add response to output
      const commandOutput = result.output || result.message || 'Command executed';
      setOutput(prev => [...prev, commandOutput, '']);
      
      // Add to command history (avoid duplicates)
      setCommandList(prev => {
        const filtered = prev.filter(cmd => cmd !== commandToSend);
        return [...filtered, commandToSend].slice(-50); // Keep last 50
      });
      setHistoryIndex(-1);
      
      // Refocus input after command
      setTimeout(() => inputRef.current?.focus(), 100);
      
    } catch (error: any) {
      setOutput(prev => [...prev, `Error: ${error.message}`, '']);
      // Refocus input even on error
      setTimeout(() => inputRef.current?.focus(), 100);
    } finally {
      setLoading(false);
    }
  };

  const handleClearOutput = () => {
    setOutput([]);
  };

  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSendCommand();
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (commandList.length > 0) {
        const newIndex = historyIndex === -1 
          ? commandList.length - 1 
          : Math.max(0, historyIndex - 1);
        setHistoryIndex(newIndex);
        setCommand(commandList[newIndex]);
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIndex !== -1) {
        const newIndex = historyIndex + 1;
        if (newIndex >= commandList.length) {
          setHistoryIndex(-1);
          setCommand('');
        } else {
          setHistoryIndex(newIndex);
          setCommand(commandList[newIndex]);
        }
      }
    }
  };

  return (
    <>
      <Title title="Device Terminal" />
      <Card>
        <CardContent>
          <Typography variant="h5" gutterBottom>
            Device Shell Terminal
          </Typography>
          
          <Stack spacing={3}>
            {/* Device Selection */}
            <FormControl fullWidth>
              <InputLabel>Select Device</InputLabel>
              <Select
                value={selectedDevice}
                onChange={(e) => {
                  setSelectedDevice(e.target.value);
                  // Find the device to get its friendly name
                  const device = devices.find(d => d.deviceId === e.target.value);
                  const displayName = device ? (device.clientId || device.deviceId) : e.target.value;
                  setOutput([`Connected to device: ${displayName}`, '']);
                }}
                label="Select Device"
              >
                {devices.map((device) => (
                  <MenuItem 
                    key={device.id} 
                    value={device.deviceId}
                  >
                    {device.clientId || device.deviceId}
                    {' '}
                    <Chip 
                      label={device.status} 
                      size="small" 
                      color={device.status === 'online' ? 'success' : 'default'}
                      sx={{ ml: 1 }}
                    />
                  </MenuItem>
                ))}
              </Select>
            </FormControl>

            {/* Terminal Output */}
            <Paper 
              ref={outputRef}
              sx={{ 
                p: 2, 
                bgcolor: '#1e1e1e', 
                color: '#d4d4d4',
                fontFamily: 'monospace',
                fontSize: '14px',
                minHeight: '400px',
                maxHeight: '600px',
                overflow: 'auto',
                whiteSpace: 'pre-wrap',
                wordBreak: 'break-word',
              }}
            >
              {output.length === 0 ? (
                <Typography variant="body2" color="text.secondary">
                  Terminal output will appear here...
                </Typography>
              ) : (
                output.map((line, index) => (
                  <div key={index}>{line || '\u00A0'}</div>
                ))
              )}
            </Paper>

            {/* Command Input */}
            <Stack direction="row" spacing={2}>
              <TextField
                fullWidth
                label="Command"
                value={command}
                onChange={(e) => setCommand(e.target.value)}
                onKeyPress={handleKeyPress}
                onKeyDown={handleKeyDown}
                disabled={!selectedDevice || loading}
                placeholder="Enter shell command (↑/↓ for history)..."
                autoComplete="off"
                inputRef={inputRef}
                autoFocus
              />
              <Button
                variant="contained"
                onClick={handleSendCommand}
                disabled={!selectedDevice || !command.trim() || loading}
                startIcon={<SendIcon />}
              >
                Send
              </Button>
              <Button
                variant="outlined"
                onClick={handleClearOutput}
                startIcon={<ClearIcon />}
              >
                Clear
              </Button>
            </Stack>

            {selectedDevice && (
              <Typography variant="caption" color="text.secondary">
                Note: Shell responses may appear in the Logs page. For real-time terminal, 
                use the standalone mqtt-terminal app.
              </Typography>
            )}
          </Stack>
        </CardContent>
      </Card>
    </>
  );
};

