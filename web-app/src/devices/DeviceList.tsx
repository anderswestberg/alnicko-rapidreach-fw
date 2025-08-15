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
} from 'react-admin';
import { Chip } from '@mui/material';

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
  const color = record.status === 'online' ? 'success' : 'error';
  return <Chip label={record.status} color={color} size="small" />;
};

export const DeviceList = () => (
  <List filters={<DeviceFilter />} sort={{ field: 'lastSeen', order: 'DESC' }}>
    <Datagrid rowClick="show">
      <TextField source="id" label="Device ID" />
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
    </Datagrid>
  </List>
);
