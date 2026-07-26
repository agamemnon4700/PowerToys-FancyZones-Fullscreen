# FancyZones fullscreen within a zone

## Goal

When the opt-in **Keep fullscreen apps in their zones** setting is enabled, a
window that is already assigned to a FancyZone remains within that zone when
the app enters borderless fullscreen. The primary scenarios are Chromium F11
and fullscreen web video. This implements the fullscreen portion of
[PowerToys issue #279](https://github.com/microsoft/PowerToys/issues/279).

Unzoned windows and all windows while the setting is disabled keep their
standard Windows behavior.

## Design map

| Responsibility | Location |
| --- | --- |
| Settings schema and default | `src/settings-ui/Settings.UI.Library/FZConfigProperties.cs` |
| Settings UI and persistence | `src/settings-ui/Settings.UI/ViewModels/FancyZonesViewModel.cs` and `SettingsXAML/Views/FancyZonesPage.xaml` |
| Native settings parsing | `src/modules/fancyzones/FancyZonesLib/Settings.h` and `Settings.cpp` |
| WinEvent hook lifecycle | `src/modules/fancyzones/FancyZones/FancyZonesApp.h` and `FancyZonesApp.cpp` |
| Fullscreen detection and zone enforcement | `src/modules/fancyzones/FancyZonesLib/FancyZones.cpp` and `WindowUtils.cpp` |
| Assigned zone lookup | `WorkAreaConfiguration`, `WorkArea`, and `LayoutAssignedWindows` |

## Runtime flow

1. FancyZones keeps an out-of-context `EVENT_OBJECT_LOCATIONCHANGE` hook active
   only while a move/size operation is running or the fullscreen setting is
   enabled.
2. The hook accepts only top-level window location events
   (`OBJID_WINDOW`/`CHILDID_SELF`) and marshals them to the FancyZones window.
3. FancyZones ignores unassigned windows. For an assigned window, it recognizes
   a fullscreen transition when the window is borderless and covers its
   monitor, allowing a small frame tolerance.
4. The combined rectangle for the window's assigned zones is converted from
   work-area coordinates to screen coordinates.
5. Two asynchronous `SetWindowPos` requests reapply that rectangle without
   restoring the window. The first delivers the app's normal size-changing
   notification so Chromium refreshes its background-fullscreen renderer
   viewport. The second suppresses that notification so Chromium cannot
   replace the requested zone bounds with the monitor rectangle.
6. When the app restores its caption or sizing frame, tracking for that window
   ends. Destroyed windows and disabled settings are also removed from the
   tracker.

Holding Shift during the initial fullscreen transition bypasses confinement
until that fullscreen session ends.

Location updates are coalesced per window. A window that repeatedly reasserts
its monitor rectangle is retried at most four times for the same zone before
that fullscreen session is released. Disabling the setting or shutting down
FancyZones asynchronously restores each constrained window's original
fullscreen rectangle before clearing its state.

## Why this path does not inject a DLL

The historical
[`peddamat/PowerToys` `peddamat/maxInZoneDLL` branch](https://github.com/peddamat/PowerToys/tree/peddamat/maxInZoneDLL)
subclasses foreign windows and rewrites `WM_WINDOWPOSCHANGING` only when
`WS_MAXIMIZE` is set. Modern Chromium fullscreen normally removes `WS_CAPTION`
and `WS_THICKFRAME` and resizes the existing top-level window without relying
on `WS_MAXIMIZE`.

An out-of-context WinEvent hook covers that transition without injecting code
into other processes, avoids architecture-specific hook DLLs, and has a much
smaller cleanup and signing surface.

## Validation

Automated checks cover native settings parsing, Settings UI persistence, and
the borderless monitor-sized detection heuristic.

Manual validation should include:

- Chrome and Edge F11 enter/exit from a single assigned zone.
- YouTube fullscreen enter/exit using both the player button and keyboard.
- Multiple Chromium fullscreen windows on one monitor, including repeated
  activation changes, with each renderer remaining sized to its zone.
- Shift bypass during entry.
- Feature disabled and unzoned-window controls.
- Multi-monitor layouts, including mixed DPI and negative monitor origins.
- A zone spanning multiple zone indices.
- Toggle changes while FancyZones is running.
- Window closure while fullscreen and PowerToys/FancyZones shutdown.
- Taskbar behavior before, during, and after fullscreen.

Exclusive fullscreen applications, elevated windows that reject cross-process
positioning, and applications that continuously reassert their monitor-sized
rectangle may not be constrainable through the non-injected path. Those cases
should fail without leaving a hook or per-window resource behind.

An application that exits fullscreen while remaining borderless at exactly the
same zone rectangle exposes no observable Win32 transition. FancyZones keeps
that session tracked until the style or bounds change; disabling the feature
during that ambiguous state restores the captured monitor rectangle. Chromium
restores its normal frame when leaving F11 or fullscreen video, so the primary
scenarios have an observable exit.
