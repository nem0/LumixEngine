high prio (feature parity with the old ui, before we can merge):
* main.unv -> demo.unv, UI is not updated
* click on ui should stop propagating ingame (tower defense, ...)

* update demo/showcase projects to use the new ui (TD and hex remains)
* error handling/reporting - element-attribute mismatch

* fixed leaks

final:
* remove the old UI

low prio or optional:
* text input
* inline 3d ui component - ui source as property, not as a resource file
* min-width?
* examples: scrolling, checkbox, drag & drop, tooltip, dropdown, slider
* DPI
* includes
* optimizations
* changes at runtime/minimal layout recompute
* grid layout?
* detailed property viewer + live edit? 
* event logging - Display recent UI events with timestamps
* border, rounded corners
* animations
* focus
* localization
* data binding, templates
* button element?
