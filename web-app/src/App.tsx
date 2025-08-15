import { Admin, Resource, Layout } from 'react-admin';
import { dataProvider } from './dataProvider';
import { Dashboard } from './Dashboard';
import { DeviceList } from './devices/DeviceList';
import { DeviceShow } from './devices/DeviceShow';
import DevicesIcon from '@mui/icons-material/Devices';
import { createTheme } from '@mui/material/styles';

// Create a custom theme
const theme = createTheme({
  palette: {
    mode: 'light',
    primary: {
      main: '#2196f3',
    },
    secondary: {
      main: '#ff9800',
    },
  },
});

// Custom layout to add app title
const CustomLayout = (props: any) => (
  <Layout {...props} appBar={() => null} />
);

function App() {
  return (
    <Admin 
      dataProvider={dataProvider}
      dashboard={Dashboard}
      theme={theme}
      layout={CustomLayout}
      title="RapidReach Admin"
    >
      <Resource 
        name="devices" 
        list={DeviceList}
        show={DeviceShow}
        icon={DevicesIcon}
      />
    </Admin>
  );
}

export default App;
