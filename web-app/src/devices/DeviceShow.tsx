import {
  Show,
  SimpleShowLayout,
  TextField,
  DateField,
  ChipField,
  FunctionField,
  useRecordContext,
  useRefresh,
  useNotify,
} from 'react-admin';
import {
  Typography,
  Box,
  Button,
  TextField as MuiTextField,
  Grid,
  Chip,
  Paper,
  CircularProgress,
  IconButton,
  Tooltip,
} from '@mui/material';
import { useState, useRef, useEffect } from 'react';
import { dataProvider } from '../dataProvider';
import SendIcon from '@mui/icons-material/Send';
import RefreshIcon from '@mui/icons-material/Refresh';

const DeviceStatus = () => {
  const record = useRecordContext();
  if (!record) return null;
  
  const color = record.status === 'online' ? 'success' : 'error';
  return <Chip label={record.status} color={color} />;
};

const DeviceMetadata = () => {
  const record = useRecordContext();
  const notify = useNotify();
  const refresh = useRefresh();
  const [loading, setLoading] = useState(false);
  const [deviceInfo, setDeviceInfo] = useState<any>(null);
  
  if (!record) return null;
  
  const refreshDeviceInfo = async () => {
    if (record.status !== 'online') {
      notify('Device must be online to refresh info', { type: 'warning' });
      return;
    }
    
    setLoading(true);
    try {
      // Execute the device info command
      const result = await dataProvider.update('devices', {
        id: record.id,
        data: { command: 'app info' },
        previousData: record,
      });
      
      // Parse the output
      const output = result.data.output || '';
      const lines = output.split('\n');
      const info: any = {};
      
      lines.forEach((line: string) => {
        if (line.includes('Firmware version:')) {
          info.firmwareVersion = line.split(':')[1]?.trim();
        } else if (line.includes('Hardware version:')) {
          info.hardwareVersion = line.split(':')[1]?.trim();
        } else if (line.includes('Board name:')) {
          info.boardName = line.split(':')[1]?.trim();
        } else if (line.includes('Device ID:')) {
          info.deviceId = line.split(':')[1]?.trim();
        } else if (line.includes('Uptime:')) {
          info.uptime = line.split(':', 2)[1]?.trim();
        } else if (line.includes('IP Address:')) {
          info.ipAddress = line.split(':')[1]?.trim();
        }
      });
      
      setDeviceInfo(info);
      notify('Device info refreshed', { type: 'success' });
      refresh(); // Also refresh the main record
    } catch (error: any) {
      const message = error.response?.data?.error || error.message || 'Failed to refresh device info';
      notify(message, { type: 'error' });
    } finally {
      setLoading(false);
    }
  };
  
  // Use either fresh data or metadata from record
  const displayInfo = deviceInfo || record.metadata || {};
  
  return (
    <Box sx={{ mt: 2 }}>
      <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
        <Typography variant="h6">
          Device Information
        </Typography>
        <Tooltip title="Refresh device info">
          <IconButton 
            onClick={refreshDeviceInfo} 
            disabled={loading || record.status !== 'online'}
            size="small"
          >
            {loading ? <CircularProgress size={20} /> : <RefreshIcon />}
          </IconButton>
        </Tooltip>
      </Box>
      <Grid container spacing={2}>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            Client ID
          </Typography>
          <Typography variant="body1">
            {record.metadata?.clientId || displayInfo.clientId || '-'}
          </Typography>
        </Grid>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            IP Address
          </Typography>
          <Typography variant="body1">
            {displayInfo.ipAddress || '-'}
          </Typography>
        </Grid>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            Firmware Version
          </Typography>
          <Typography variant="body1">
            {displayInfo.firmwareVersion || '-'}
          </Typography>
        </Grid>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            Uptime
          </Typography>
          <Typography variant="body1">
            {displayInfo.uptime || 
             (record.metadata?.uptime 
              ? `${Math.floor(record.metadata.uptime / 3600)}h ${Math.floor((record.metadata.uptime % 3600) / 60)}m`
              : '-')}
          </Typography>
        </Grid>
        {displayInfo.hardwareVersion && (
          <Grid size={{ xs: 12, sm: 6 }}>
            <Typography variant="body2" color="textSecondary">
              Hardware Version
            </Typography>
            <Typography variant="body1">
              {displayInfo.hardwareVersion}
            </Typography>
          </Grid>
        )}
        {displayInfo.boardName && (
          <Grid size={{ xs: 12, sm: 6 }}>
            <Typography variant="body2" color="textSecondary">
              Board Name
            </Typography>
            <Typography variant="body1">
              {displayInfo.boardName}
            </Typography>
          </Grid>
        )}
      </Grid>
    </Box>
  );
};

const CommandExecutor = () => {
  const record = useRecordContext();
  const refresh = useRefresh();
  const notify = useNotify();
  const [command, setCommand] = useState('');
  const [commandHistory, setCommandHistory] = useState<Array<{command: string, output: string, timestamp: Date}>>([]);
  const [commandList, setCommandList] = useState<string[]>([]); // List of executed commands for history
  const [historyIndex, setHistoryIndex] = useState(-1); // Current position in command history
  const [loading, setLoading] = useState(false);
  const terminalRef = useRef<HTMLDivElement>(null);
  
  // Load command history from localStorage on mount
  useEffect(() => {
    if (record) {
      const savedHistory = localStorage.getItem(`deviceCommandHistory_${record.id}`);
      if (savedHistory) {
        try {
          const parsed = JSON.parse(savedHistory);
          setCommandList(parsed);
        } catch (e) {
          console.error('Failed to parse saved command history:', e);
        }
      }
    }
  }, [record?.id]);
  
  // Save command history to localStorage when it changes
  useEffect(() => {
    if (record && commandList.length > 0) {
      // Keep only the last 50 commands
      const historyToSave = commandList.slice(-50);
      localStorage.setItem(`deviceCommandHistory_${record.id}`, JSON.stringify(historyToSave));
    }
  }, [commandList, record?.id]);
  
  if (!record || record.status !== 'online') {
    return (
      <Paper sx={{ p: 2, mt: 2, backgroundColor: '#f5f5f5' }}>
        <Typography color="textSecondary">
          Device must be online to execute commands
        </Typography>
      </Paper>
    );
  }
  
  const executeCommand = async () => {
    if (!command.trim()) {
      notify('Please enter a command', { type: 'warning' });
      return;
    }
    
    setLoading(true);
    const currentCommand = command.trim();
    setCommand(''); // Clear input immediately for better UX
    
    try {
      const result = await dataProvider.update('devices', {
        id: record.id,
        data: { command: currentCommand },
        previousData: record,
      });
      
      const output = result.data.output || 'Command executed successfully';
      setCommandHistory(prev => [...prev, {
        command: currentCommand,
        output: output,
        timestamp: new Date()
      }]);
      
      // Add to command list for arrow key navigation (avoid duplicates)
      setCommandList(prev => {
        const filtered = prev.filter(cmd => cmd !== currentCommand);
        return [...filtered, currentCommand];
      });
      setHistoryIndex(-1); // Reset history navigation
      
      notify('Command executed successfully', { type: 'success' });
      refresh();
      
      // Scroll to bottom after adding new entry
      setTimeout(() => {
        if (terminalRef.current) {
          terminalRef.current.scrollTop = terminalRef.current.scrollHeight;
        }
      }, 100);
    } catch (error: any) {
      const message = error.response?.data?.error || error.message || 'Failed to execute command';
      notify(message, { type: 'error' });
      setCommandHistory(prev => [...prev, {
        command: currentCommand,
        output: `Error: ${message}`,
        timestamp: new Date()
      }]);
      
      // Still add to command list even on error (avoid duplicates)
      setCommandList(prev => {
        const filtered = prev.filter(cmd => cmd !== currentCommand);
        return [...filtered, currentCommand];
      });
      setHistoryIndex(-1);
    } finally {
      setLoading(false);
    }
  };
  
  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      executeCommand();
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
    <Box sx={{ mt: 3 }}>
      <Typography variant="h6" gutterBottom>
        Execute Command
      </Typography>
      
      {/* Terminal History */}
      <Paper 
        ref={terminalRef}
        sx={{ 
          mb: 2,
          p: 2, 
          backgroundColor: '#1e1e1e',
          color: '#ffffff',
          fontFamily: 'monospace',
          fontSize: '0.875rem',
          minHeight: '300px',
          maxHeight: '500px',
          overflowY: 'auto',
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-all',
        }}
      >
        {commandHistory.length === 0 ? (
          <Typography sx={{ color: '#888', fontFamily: 'monospace' }}>
            Ready to execute commands...
          </Typography>
        ) : (
          commandHistory.map((entry, index) => (
            <Box key={index} sx={{ mb: 2 }}>
              <Box sx={{ color: '#00ff00' }}>
                $ {entry.command}
              </Box>
              <Box sx={{ color: '#ffffff', mt: 0.5 }}>
                {entry.output}
              </Box>
              {index < commandHistory.length - 1 && (
                <Box sx={{ borderBottom: '1px solid #333', mt: 2, mb: 2 }} />
              )}
            </Box>
          ))
        )}
      </Paper>
      
      {/* Command Input */}
      <Box sx={{ display: 'flex', gap: 1 }}>
        <MuiTextField
          fullWidth
          variant="outlined"
          placeholder="Enter command (e.g., device id, help) - Use ↑/↓ for history"
          value={command}
          onChange={(e) => setCommand(e.target.value)}
          onKeyPress={handleKeyPress}
          onKeyDown={handleKeyDown}
          disabled={loading}
          sx={{
            '& .MuiOutlinedInput-root': {
              fontFamily: 'monospace',
            }
          }}
        />
        <Button
          variant="contained"
          onClick={executeCommand}
          disabled={loading}
          startIcon={loading ? <CircularProgress size={20} /> : <SendIcon />}
        >
          Execute
        </Button>
      </Box>
    </Box>
  );
};

export const DeviceShow = () => (
  <Show>
    <SimpleShowLayout>
      <TextField source="id" label="Device ID" />
      <ChipField source="type" label="Type" />
      <FunctionField label="Status" render={() => <DeviceStatus />} />
      <DateField source="lastSeen" label="Last Seen" showTime />
      <DeviceMetadata />
      <CommandExecutor />
    </SimpleShowLayout>
  </Show>
);
