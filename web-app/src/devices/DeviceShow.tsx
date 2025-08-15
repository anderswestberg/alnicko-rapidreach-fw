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
  Card,
  CardContent,
  Typography,
  Box,
  Button,
  TextField as MuiTextField,
  Grid,
  Chip,
  Paper,
} from '@mui/material';
import { useState } from 'react';
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
  const [output, setOutput] = useState('');
  const [loading, setLoading] = useState(false);
  
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
    try {
      const result = await dataProvider.update('devices', {
        id: record.id,
        data: { command: command.trim() },
        previousData: record,
      });
      
      setOutput(result.data.output || 'Command executed successfully');
      notify('Command executed successfully', { type: 'success' });
      refresh();
    } catch (error: any) {
      const message = error.response?.data?.error || error.message || 'Failed to execute command';
      notify(message, { type: 'error' });
      setOutput(`Error: ${message}`);
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
      <Box sx={{ display: 'flex', gap: 1 }}>
        <MuiTextField
          fullWidth
          variant="outlined"
          placeholder="Enter command (e.g., device id, help)"
          value={command}
          onChange={(e) => setCommand(e.target.value)}
          onKeyPress={handleKeyPress}
          disabled={loading}
        />
        <Button
          variant="contained"
          onClick={executeCommand}
          disabled={loading}
          startIcon={<SendIcon />}
        >
          Execute
        </Button>
      </Box>
      
      {output && (
        <Paper 
          sx={{ 
            mt: 2, 
            p: 2, 
            backgroundColor: '#f5f5f5',
            fontFamily: 'monospace',
            fontSize: '0.875rem',
            whiteSpace: 'pre-wrap',
            wordBreak: 'break-all',
          }}
        >
          {output}
        </Paper>
      )}
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
