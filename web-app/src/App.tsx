import { Resource } from 'react-admin';
import { Admin } from '@react-admin/ra-enterprise';
import { dataProvider } from './dataProvider';
import { Dashboard } from './Dashboard';
import { DeviceList } from './devices/DeviceList';
import { DeviceShow } from './devices/DeviceShow';
import DevicesIcon from '@mui/icons-material/Devices';
import ListAltIcon from '@mui/icons-material/ListAlt';
import VolumeUpIcon from '@mui/icons-material/VolumeUp';
import TerminalIcon from '@mui/icons-material/Terminal';
import { createTheme } from '@mui/material/styles';
import { LogsList } from './logs/LogsList';
import { AudioAlerts } from './audio/AudioAlerts';
import { DeviceTerminal } from './terminal/DeviceTerminal';
// Navigation components temporarily removed due to compatibility issues
// import { MultiLevelMenu, MenuItemCategory } from '@react-admin/ra-navigation';

// Create a custom theme with RapidReach red
const theme = createTheme({
  palette: {
    mode: 'light',
    primary: {
      main: '#d32f2f', // RapidReach red
      dark: '#9a0007',
      light: '#ff6659',
    },
    secondary: {
      main: '#757575', // Grey for secondary actions
      dark: '#494949',
      light: '#a4a4a4',
    },
    background: {
      default: '#fafafa',
      paper: '#ffffff',
    },
  },
  typography: {
    h6: {
      fontWeight: 600,
    },
  },
});

// Custom menu and layout temporarily removed

function App() {
  return (
    <Admin 
      dataProvider={dataProvider}
      dashboard={Dashboard}
      theme={theme}
      //layout={CustomLayout}
      title="RapidReach Control Center"
      // Enterprise Edition features
      lightTheme={theme}
      darkTheme={createTheme({
        palette: {
          mode: 'dark',
          primary: {
            main: '#ff6659', // Lighter red for dark mode
          },
          secondary: {
            main: '#757575',
          },
        },
      })}
    >
      <Resource 
        name="devices" 
        list={DeviceList}
        show={DeviceShow}
        icon={DevicesIcon}
        recordRepresentation="clientId"
      />
      <Resource 
        name="logs" 
        list={LogsList}
        icon={ListAltIcon}
        recordRepresentation="message"
      />
      <Resource 
        name="audio" 
        list={AudioAlerts}
        icon={VolumeUpIcon}
        options={{ label: 'Audio Alerts' }}
      />
      <Resource 
        name="terminal" 
        list={DeviceTerminal}
        icon={TerminalIcon}
        options={{ label: 'Terminal' }}
      />
    </Admin>
  );
}

export default App;
