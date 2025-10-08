import {
  List,
  Datagrid,
  TextField,
  DateField,
  SelectInput,
  TextInput,
  Filter,
  ChipField,
  FunctionField,
  useRecordContext,
  SimpleList,
} from 'react-admin';
import { Chip, IconButton, Tooltip, Stack, useMediaQuery } from '@mui/material';
import type { Theme } from '@mui/material/styles';
import TerminalIcon from '@mui/icons-material/Terminal';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import { useNavigate } from 'react-router-dom';

const DeviceFilter = (props: any) => (
  <Filter {...props}>
    <TextInput label="Search" source="q" alwaysOn />
    <SelectInput
      label="Status"
      source="status"
      choices={[
        { id: 'online', name: 'Online' },
        { id: 'offline', name: 'Offline' },
      ]}
    />
    <SelectInput
      label="Type"
      source="type"
      choices={[
        { id: 'speaker', name: 'Speaker' },
        { id: 'sensor', name: 'Sensor' },
        { id: 'unknown', name: 'Unknown' },
      ]}
    />
  </Filter>
);

const StatusField = ({ record }: any) => {
  if (!record) return null;
  const color = record.status === 'online' ? 'success' : 'error';
  return <Chip label={record.status} color={color} size="small" />;
};

const ActionsField = () => {
  const record = useRecordContext();
  const navigate = useNavigate();
  
  if (!record) return null;
  
  const deviceId = record.clientId || record.id;
  
  const handleTerminalClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    // Pre-select device and navigate
    sessionStorage.setItem('preselectedDevice', deviceId);
    navigate('/terminal');
  };
  
  const handleAudioClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    // Navigate and pre-select device
    navigate('/audio');
    // Store selected device in sessionStorage for AudioAlerts to pick up
    sessionStorage.setItem('preselectedDevice', deviceId);
  };
  
  return (
    <Stack direction="row" spacing={1}>
      <Tooltip title="Open Terminal">
        <IconButton
          size="small"
          onClick={handleTerminalClick}
          color="primary"
        >
          <TerminalIcon />
        </IconButton>
      </Tooltip>
      <Tooltip title="Send Audio Alert">
        <IconButton
          size="small"
          onClick={handleAudioClick}
          color="secondary"
        >
          <VolumeUpIcon />
        </IconButton>
      </Tooltip>
    </Stack>
  );
};

export const DeviceList = () => {
  const isSmall = useMediaQuery<Theme>((theme) => theme.breakpoints.down('md'));

  return (
    <List filters={<DeviceFilter />} sort={{ field: 'lastSeen', order: 'DESC' }}>
      {isSmall ? (
        <SimpleList
          primaryText={(record) => record.clientId || record.id}
          secondaryText={(record) => 
            `${record.type} • ${record.status} • ${record.metadata?.firmwareVersion || 'unknown'}`
          }
          tertiaryText={(record) => 
            `IP: ${record.metadata?.ipAddress || 'N/A'} • Last seen: ${new Date(record.lastSeen).toLocaleString()}`
          }
          linkType="show"
          rowStyle={(record) => ({
            borderLeft: `4px solid ${record.status === 'online' ? '#4caf50' : '#f44336'}`,
          })}
        />
      ) : (
        <Datagrid rowClick="show">
          <TextField source="clientId" label="Device ID" />
          <ChipField source="type" label="Type" />
          <FunctionField label="Status" render={StatusField} />
          <DateField source="lastSeen" label="Last Seen" showTime />
          <FunctionField
            label="IP Address"
            render={(record: any) => record.metadata?.ipAddress || '-'}
          />
          <FunctionField
            label="Firmware"
            render={(record: any) => record.metadata?.firmwareVersion || '-'}
          />
          <FunctionField
            label="Uptime"
            render={(record: any) => {
              const uptime = record.metadata?.uptime;
              if (!uptime) return '-';
              
              const days = Math.floor(uptime / 86400);
              const hours = Math.floor((uptime % 86400) / 3600);
              const minutes = Math.floor((uptime % 3600) / 60);
              
              if (days > 0) {
                return `${days}d ${hours}h ${minutes}m`;
              } else if (hours > 0) {
                return `${hours}h ${minutes}m`;
              } else {
                return `${minutes}m`;
              }
            }}
          />
          <FunctionField
            label="Actions"
            render={() => <ActionsField />}
            textAlign="right"
          />
        </Datagrid>
      )}
    </List>
  );
};
