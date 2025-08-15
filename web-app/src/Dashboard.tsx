import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, Grid, Typography, Box, Alert } from '@mui/material';
import { PieChart, Pie, Cell, ResponsiveContainer, Legend, Tooltip } from 'recharts';
import DevicesIcon from '@mui/icons-material/Devices';
import CheckCircleIcon from '@mui/icons-material/CheckCircle';
import CancelIcon from '@mui/icons-material/Cancel';
import BusinessIcon from '@mui/icons-material/Business';
import { getDeviceStats } from './dataProvider';

interface DeviceStats {
  total: number;
  connected: number;
  offline: number;
  byType: Record<string, number>;
  timestamp: string;
}

const COLORS = {
  connected: '#4caf50', // Keep green for connected
  offline: '#d32f2f',   // RapidReach red for offline
  speaker: '#d32f2f',   // RapidReach red for primary device type
  sensor: '#ff9800',    // Orange for sensors
  unknown: '#9e9e9e',   // Grey for unknown
};

export const Dashboard = () => {
  const [stats, setStats] = useState<DeviceStats | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchStats = async () => {
      try {
        setLoading(true);
        const data = await getDeviceStats();
        setStats(data);
        setError(null);
      } catch (err) {
        setError('Failed to fetch device statistics');
        console.error('Error fetching stats:', err);
      } finally {
        setLoading(false);
      }
    };

    fetchStats();
    
    // Refresh stats every 10 seconds
    const interval = setInterval(fetchStats, 10000);
    
    return () => clearInterval(interval);
  }, []);

  if (loading && !stats) {
    return (
      <Box display="flex" justifyContent="center" alignItems="center" height="400px">
        <Typography>Loading...</Typography>
      </Box>
    );
  }

  if (error) {
    return (
      <Box display="flex" justifyContent="center" alignItems="center" height="400px">
        <Typography color="error">{error}</Typography>
      </Box>
    );
  }

  if (!stats) {
    return null;
  }

  const statusData = [
    { name: 'Connected', value: stats.connected, color: COLORS.connected },
    { name: 'Offline', value: stats.offline, color: COLORS.offline },
  ];

  const typeData = Object.entries(stats.byType).map(([type, count]) => ({
    name: type.charAt(0).toUpperCase() + type.slice(1),
    value: count,
    color: COLORS[type as keyof typeof COLORS] || COLORS.unknown,
  }));

  return (
    <Box sx={{ p: 3 }}>
      <Typography variant="h4" gutterBottom>
        Device Dashboard
      </Typography>
      
      <Alert 
        severity="info" 
        icon={<BusinessIcon />}
        sx={{ mb: 3 }}
      >
        <Typography variant="body2">
          <strong>RapidReach Admin</strong> - Powered by React-Admin Enterprise Edition
        </Typography>
        <Typography variant="caption" display="block" sx={{ mt: 0.5 }}>
          Using Enterprise Admin, Layout, and Navigation components with advanced theming (light/dark mode support)
        </Typography>
      </Alert>
      
      <Grid container spacing={3}>
        {/* Total Devices */}
        <Grid size={{ xs: 12, sm: 6, md: 3 }}>
          <Card>
            <CardContent>
              <Box display="flex" alignItems="center" justifyContent="space-between">
                <Box>
                  <Typography color="textSecondary" gutterBottom>
                    Total Devices
                  </Typography>
                  <Typography variant="h3">
                    {stats.total}
                  </Typography>
                </Box>
                <DevicesIcon sx={{ fontSize: 48, color: '#d32f2f' }} />
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* Connected Devices */}
        <Grid size={{ xs: 12, sm: 6, md: 3 }}>
          <Card>
            <CardContent>
              <Box display="flex" alignItems="center" justifyContent="space-between">
                <Box>
                  <Typography color="textSecondary" gutterBottom>
                    Connected
                  </Typography>
                  <Typography variant="h3" sx={{ color: COLORS.connected }}>
                    {stats.connected}
                  </Typography>
                </Box>
                <CheckCircleIcon sx={{ fontSize: 48, color: COLORS.connected }} />
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* Offline Devices */}
        <Grid size={{ xs: 12, sm: 6, md: 3 }}>
          <Card>
            <CardContent>
              <Box display="flex" alignItems="center" justifyContent="space-between">
                <Box>
                  <Typography color="textSecondary" gutterBottom>
                    Offline
                  </Typography>
                  <Typography variant="h3" sx={{ color: COLORS.offline }}>
                    {stats.offline}
                  </Typography>
                </Box>
                <CancelIcon sx={{ fontSize: 48, color: COLORS.offline }} />
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* Connection Rate */}
        <Grid size={{ xs: 12, sm: 6, md: 3 }}>
          <Card>
            <CardContent>
              <Box>
                <Typography color="textSecondary" gutterBottom>
                  Connection Rate
                </Typography>
                <Typography variant="h3">
                  {stats.total > 0 
                    ? Math.round((stats.connected / stats.total) * 100) 
                    : 0}%
                </Typography>
              </Box>
            </CardContent>
          </Card>
        </Grid>

        {/* Device Status Chart */}
        <Grid size={{ xs: 12, md: 6 }}>
          <Card>
            <CardHeader title="Device Status" />
            <CardContent>
              <ResponsiveContainer width="100%" height={300}>
                <PieChart>
                  <Pie
                    data={statusData}
                    cx="50%"
                    cy="50%"
                    labelLine={false}
                    label={({ value }) => value}
                    outerRadius={80}
                    fill="#8884d8"
                    dataKey="value"
                  >
                    {statusData.map((entry, index) => (
                      <Cell key={`cell-${index}`} fill={entry.color} />
                    ))}
                  </Pie>
                  <Tooltip />
                  <Legend />
                </PieChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>

        {/* Device Types Chart */}
        <Grid size={{ xs: 12, md: 6 }}>
          <Card>
            <CardHeader title="Device Types" />
            <CardContent>
              <ResponsiveContainer width="100%" height={300}>
                <PieChart>
                  <Pie
                    data={typeData}
                    cx="50%"
                    cy="50%"
                    labelLine={false}
                    label={({ name, value }) => `${name}: ${value}`}
                    outerRadius={80}
                    fill="#8884d8"
                    dataKey="value"
                  >
                    {typeData.map((entry, index) => (
                      <Cell key={`cell-${index}`} fill={entry.color} />
                    ))}
                  </Pie>
                  <Tooltip />
                  <Legend />
                </PieChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>

        {/* Last Update */}
        <Grid size={12}>
          <Typography variant="body2" color="textSecondary" align="center">
            Last updated: {new Date(stats.timestamp).toLocaleString()}
          </Typography>
        </Grid>
      </Grid>
    </Box>
  );
};
