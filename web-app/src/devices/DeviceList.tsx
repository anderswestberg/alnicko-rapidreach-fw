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
import ListAltIcon from '@mui/icons-material/ListAlt';

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
  
  if (!record) return null;
  
  // Use deviceId (numeric like "313938") for pre-selection
  const deviceId = record.deviceId || record.id;
  
  const handleTerminalClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    // Pre-select device BEFORE navigation
    sessionStorage.setItem('preselectedDevice', deviceId);
    // Small delay to ensure sessionStorage is written
    setTimeout(() => {
      window.location.hash = '#/terminal';
    }, 0);
  };
  
  const handleAudioClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    // Pre-select device BEFORE navigation
    sessionStorage.setItem('preselectedDevice', deviceId);
    // Small delay to ensure sessionStorage is written
    setTimeout(() => {
      window.location.hash = '#/audio';
    }, 0);
  };

  const handleLogsClick = (e: React.MouseEvent) => {
    e.stopPropagation();
    // Navigate to logs with device filter using hash router
    window.location.hash = `#/logs?filter=${encodeURIComponent(JSON.stringify({ deviceId }))}`;
  };
  
  return (
    <Stack direction="row" spacing={1}>
      <Tooltip title="View Logs">
        <IconButton
          size="small"
          onClick={handleLogsClick}
          color="default"
        >
          <ListAltIcon fontSize="small" />
        </IconButton>
      </Tooltip>
      <Tooltip title="Open Terminal">
        <IconButton
          size="small"
          onClick={handleTerminalClick}
          color="primary"
        >
          <TerminalIcon fontSize="small" />
        </IconButton>
      </Tooltip>
      <Tooltip title="Send Audio Alert">
        <IconButton
          size="small"
          onClick={handleAudioClick}
          color="secondary"
        >
          <VolumeUpIcon fontSize="small" />
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
            `${record.type} • ${record.status} • ${record.firmwareVersion || 'unknown'}`
          }
          tertiaryText={(record) => 
            `IP: ${record.ipAddress || 'N/A'} • Last seen: ${new Date(record.lastSeen).toLocaleString()}`
          }
          linkType="show"
          rowStyle={(record) => ({
            borderLeft: `4px solid ${record.status === 'online' ? '#4caf50' : '#f44336'}`,
          })}
        />
      ) : (
        <Datagrid rowClick="show">
          <TextField source="deviceId" label="Device ID" />
          <TextField source="clientId" label="Client ID" />
          <ChipField source="type" label="Type" />
          <FunctionField label="Status" render={StatusField} />
          <DateField source="lastSeen" label="Last Seen" showTime />
          <FunctionField
            label="IP Address"
            render={(record: any) => record.ipAddress || '-'}
          />
          <FunctionField
            label="Firmware"
            render={(record: any) => record.firmwareVersion || '-'}
          />
          <FunctionField
            label="Uptime"
            render={(record: any) => {
              const uptime = record.uptime;
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
