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
2. The hook maps child-window location events to their top-level root and
   coalesces them before marshaling them to the FancyZones window. This lets a
   stale Chromium renderer or GPU surface trigger verification of its root.
3. FancyZones ignores unassigned windows. For an assigned window, it recognizes
   a fullscreen transition when the window is borderless and covers its
   monitor, allowing a small frame tolerance.
4. The combined rectangle for the window's assigned zones is converted from
   work-area coordinates to screen coordinates.
5. Chromium receives a bounded synthetic `WM_WINDOWPOSCHANGING` notification
   so it refreshes its background-fullscreen renderer state without committing
   an intermediate monitor-sized window. One asynchronous `SetWindowPos` then
   enforces the zone while suppressing Chromium's monitor-bounds rewrite.
   Other apps retain the normal notified request before enforcement.
6. A shared short verification timer coalesces location events while a
   correction is pending. It verifies both the Chromium root and its visible
   renderer/GPU child sizes. An oversized direct Chromium surface is resized
   to the root client area as a tightly scoped fallback.
7. Bare Escape and F11 presses synchronously record an exit intent for the
   foreground tracked window before the app receives the key. Root and renderer
   corrections pause for half a second so a queued repair cannot race the
   app's fullscreen exit. A restored frame ends tracking immediately; if the
   app ignores the key, verification resumes confinement after the deadline.
8. When the app restores its caption or sizing frame, tracking for that window
   ends. Destroyed windows and disabled settings are also removed from the
   tracker.

Holding Shift during the initial fullscreen transition bypasses confinement
until that fullscreen session ends.

Location updates are coalesced per window. Root geometry and Chromium surface
repairs use separate retry budgets. A verified root correction clears its
failure budget, while four consecutive failed root verifications release that
fullscreen session. Surface repair uses a cooldown after four attempts and
never releases or delays an otherwise correctly constrained root. Disabling
the setting or shutting down FancyZones asynchronously restores each
constrained window's original fullscreen rectangle before clearing its state.

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
- Exit attempts made while a root or renderer correction is pending, including
  an ignored Escape/F11 that must resume confinement after the exit-intent
  deadline.
- Multiple Chromium fullscreen windows on one monitor, including repeated
  activation changes, with each renderer remaining sized to its zone.
- No second monitor-sized root transaction after FancyZones starts a
  correction, and no stale renderer/GPU child after its verification tick.
- Chromium still reaches its zone when the bounded position notification is
  rejected or times out.
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
