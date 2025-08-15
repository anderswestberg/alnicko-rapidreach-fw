# RapidReach Admin Web App

A React-based admin dashboard for managing RapidReach devices using react-admin.

## Features

- **Dashboard**: Real-time device statistics showing connected/offline devices
- **Device List**: View all devices with filtering and sorting
- **Device Details**: View device metadata and execute commands remotely
- **Real-time Updates**: Dashboard refreshes every 10 seconds

## Setup

### Prerequisites

- Node.js 16+
- Device server running on port 3002

### Environment Configuration

Create a `.env` file in the web-app directory:

```env
VITE_API_URL=http://localhost:3002/api
VITE_API_KEY=your-secure-api-key-here
```

### Installation

```bash
npm install
```

### Development

```bash
npm run dev
```

The app will be available at http://localhost:5173

### Build

```bash
npm run build
```

## Architecture

- **Vite**: Fast build tool and dev server
- **React 18**: UI framework
- **TypeScript**: Type safety
- **react-admin**: Admin framework
- **Material-UI**: Component library
- **Recharts**: Charts for dashboard
- **Axios**: HTTP client

## API Integration

The app connects to the device server API at `http://localhost:3002/api` (configurable via environment variable).

### Endpoints Used

- `GET /api/devices/stats` - Dashboard statistics
- `GET /api/devices` - List all devices
- `GET /api/devices/:id` - Get device details
- `POST /api/devices/:id/execute` - Execute command on device

## Device Status

Devices are considered:
- **Online**: Received heartbeat within last 60 seconds
- **Offline**: No heartbeat for more than 60 seconds

## Command Execution

Commands can be executed on online devices from the device detail page. Examples:
- `help` - Show available commands
- `device id` - Get device ID
- `net iface` - Show network interface
- `led on 0` - Turn on LED

## Development Notes

### Adding New Features

1. **New Resource**: Add to `App.tsx` as a new `<Resource>`
2. **New Dashboard Widget**: Add to `Dashboard.tsx`
3. **API Changes**: Update `dataProvider.ts`

### Styling

The app uses Material-UI with a custom theme defined in `App.tsx`. Colors:
- Primary: Blue (#2196f3)
- Secondary: Orange (#ff9800)
- Success: Green (#4caf50)
- Error: Red (#f44336)