# RapidReach Admin - React-Admin Enterprise Edition

This web application is built with **React-Admin Enterprise Edition**, providing advanced features for managing RapidReach devices.

## Enterprise Features Installed

Based on the [React-Admin Enterprise Edition](https://react-admin-ee.marmelab.com/whats-new) components:

### Core Enterprise Package
- Package: `@react-admin/ra-enterprise` (v12.1.0)
- Enhanced Admin component with:
  - Advanced theming capabilities
  - Built-in tour system
  - Enhanced layout options
  - Improved performance optimizations

### 1. **Editable Datagrid** (v5.2.2)
- Package: `@react-admin/ra-editable-datagrid`
- Allows inline editing of data directly in the grid
- Ready to implement in the device list view

### 2. **Form Layout** (v5.15.0)
- Package: `@react-admin/ra-form-layout`
- Advanced form layouts including:
  - Accordion forms
  - Wizard forms
  - Stepper forms
  - Dual column layouts

### 3. **RBAC** (v6.2.0)
- Package: `@react-admin/ra-rbac`
- Role-Based Access Control
- Fine-grained permissions
- Can show read-only fields based on permissions

### 4. **Relationships** (v5.3.1)
- Package: `@react-admin/ra-relationships`
- Advanced relationship management
- Reference inputs and fields
- Many-to-many relationships

### 5. **Tree** (v7.0.2)
- Package: `@react-admin/ra-tree`
- Hierarchical data display
- Tree inputs with MUI Popover
- Expandable tree navigation

### 6. **Navigation** (v6.1.0)
- Package: `@react-admin/ra-navigation`
- Multi-level menu system
- Breadcrumb navigation
- Menu categories with icons

## Current Implementation

The application currently showcases:
- **RapidReach Red Theme**: Professional branding with #d32f2f primary color
- **Enterprise Edition Banner**: Visible on the dashboard
- **Multi-level Navigation**: Hierarchical menu structure (ready to expand)
- **Device Management**: Full CRUD operations for devices

## Future Enhancements

With the Enterprise Edition packages installed, you can:
1. Enable inline editing of device properties
2. Add role-based permissions for different user types
3. Create hierarchical device organization
4. Implement advanced search and filtering
5. Add audit logging for compliance
6. Enable real-time updates with `@react-admin/ra-realtime`

## Configuration

The Enterprise Edition packages are configured via:
- `.npmrc`: Points to the Marmelab private registry
- Authentication: Handled via npm configuration

## Development

```bash
# Install dependencies
npm install --legacy-peer-deps

# Start development server
npm run dev

# Build for production
npm run build
```

## License

This application uses React-Admin Enterprise Edition which requires a valid license from Marmelab.
