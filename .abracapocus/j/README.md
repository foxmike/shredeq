# j

## Goal

GUI: LookAndFeel, main EQ workspace, band nodes, inspector, Guitar Intelligence drawer, view modes, editor layout.

See SHREDEQ_SPEC.md section 13 for complete GUI specification. Correct LookAndFeel_V4 overrides with individual parameters (not PopupMenu::Item API). Default 1360x820, min 980x620, resizable, high-DPI.

## Notes

- Async: Wave 1: j.1 (LookAndFeel) standalone. Wave 2: j.2 (EQWorkspace + BandNode) and j.3 (InspectorPanel) depend on j.1 but independent of each other. Wave 3: j.4 (IntelligenceDrawer) depends on j.1. Wave 4: j.5 (PluginEditor assembly) fan-in — sole Editor/CMakeLists owner.
- Risk: Phase J is antigravity-heavy. PluginEditor fan-in in j.5. Known JUCE API: drawPopupMenuItem signature.
