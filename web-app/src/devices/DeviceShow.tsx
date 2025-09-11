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
} from '@mui/material';
import { useState, useRef } from 'react';
import { dataProvider } from '../dataProvider';
import SendIcon from '@mui/icons-material/Send';

const DeviceStatus = () => {
  const record = useRecordContext();
  if (!record) return null;
  
  const color = record.status === 'online' ? 'success' : 'error';
  return <Chip label={record.status} color={color} />;
};

const DeviceMetadata = () => {
  const record = useRecordContext();
  if (!record || !record.metadata) return null;
  
  return (
    <Box sx={{ mt: 2 }}>
      <Typography variant="h6" gutterBottom>
        Device Information
      </Typography>
      <Grid container spacing={2}>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            Client ID
          </Typography>
          <Typography variant="body1">
            {record.metadata.clientId || '-'}
          </Typography>
        </Grid>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            IP Address
          </Typography>
          <Typography variant="body1">
            {record.metadata.ipAddress || '-'}
          </Typography>
        </Grid>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            Firmware Version
          </Typography>
          <Typography variant="body1">
            {record.metadata.firmwareVersion || '-'}
          </Typography>
        </Grid>
        <Grid size={{ xs: 12, sm: 6 }}>
          <Typography variant="body2" color="textSecondary">
            Uptime
          </Typography>
          <Typography variant="body1">
            {record.metadata.uptime 
              ? `${Math.floor(record.metadata.uptime / 3600)}h ${Math.floor((record.metadata.uptime % 3600) / 60)}m`
              : '-'}
          </Typography>
        </Grid>
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
  const [loading, setLoading] = useState(false);
  const terminalRef = useRef<HTMLDivElement>(null);
  
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
          placeholder="Enter command (e.g., device id, help)"
          value={command}
          onChange={(e) => setCommand(e.target.value)}
          onKeyPress={handleKeyPress}
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
