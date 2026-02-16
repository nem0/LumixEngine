# Missing UI Features

This document outlines basic UI features that are currently missing from the Lumix Engine UI system, based on the existing documentation. The system provides a minimal immediate-mode GUI framework suitable for games, but lacks many common elements and capabilities found in more comprehensive UI libraries.

## Missing Core Elements/Widgets

The UI system currently supports only a basic set of elements (`panel`, `text`, `button`, `image`, `input`, `canvas`). The following common widgets are not available:

- **Checkbox**: Boolean toggle input element
- **Radio buttons**: Grouped single-selection inputs
- **Dropdown/Select/Combo box**: Compact selector for lists of options
- **Slider/Scrollbar**: Range input controls and scrolling mechanisms
- **Progress bar**: Visual progress indicators
- **Textarea/Multi-line input**: Multi-line text input (current `input` appears single-line only)
- **Tabs/Tab control**: Tabbed interface components
- **Tooltip**: Hover-based help text displays
- **Menu/Menu bar**: Hierarchical menu systems
- **Dialog/Modal**: Popup dialogs and modal overlays
- **Table/Grid**: Tabular data display components
- **List/List box**: Scrollable list components
- **Separator/Divider**: Visual separators between UI sections

## Missing Layout Features

While basic layout is supported via `direction`, `justify-content`, and `wrap`, several advanced layout capabilities are missing:

- **Scrolling/Scrollable areas**: Scrollbars and scrollable viewports for content larger than the container
- **Advanced flexbox features**: Missing `align-items`, `flex-grow`, `flex-shrink`, `align-self` for more flexible layouts
- **Grid layout**: CSS Grid equivalent for complex 2D layouts
- **Absolute positioning**: `position: absolute` or similar for precise overlay positioning
- **Z-index control**: Explicit layering control (current system uses only implicit stacking based on tree order)

## Missing Styling Features

The CSS-like styling system is quite basic. The following styling capabilities are not supported:

- **Pseudo-classes**: `:hover`, `:focus`, `:active`, `:disabled` state selectors
- **Pseudo-elements**: `::before`, `::after` for decorative content
- **Animations/Transitions**: Animated property changes
- **Themes/Skinning**: Theme switching or global style variables
- **Responsive design**: Media queries or breakpoint-based styling
- **Border styling**: Border width, style, color, and radius properties
- **Shadow effects**: `box-shadow` and `text-shadow`
- **Opacity control**: Alpha channel/transparency beyond `visible`
- **Cursor styling**: Dynamic cursor changes on interaction

## Missing Interaction Features

User interaction capabilities are limited:

- **Keyboard navigation**: Tab order, arrow key navigation, keyboard shortcuts
- **Focus management**: Explicit focus control and focus indicators
- **Drag and drop**: Drag and drop interaction support
- **Context menus**: Right-click context menu support
- **Form validation**: Input validation and error state handling
- **Accessibility**: ARIA attributes, screen reader support, keyboard-only navigation

## Missing Advanced Features

Several advanced UI system features are not documented:

- **Data binding**: Automatic UI updates from data model changes
- **State management**: Built-in state persistence and management
- **Event system details**: Comprehensive event handling documentation beyond basic clicks
- **Internationalization**: Text localization and RTL language support
- **Performance optimizations**: Culling, batching, and rendering optimizations
- **Serialization**: Save/load UI state and definitions

## Implementation Notes
