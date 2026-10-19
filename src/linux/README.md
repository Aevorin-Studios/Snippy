# Linux Implementation Roadmap

This directory is reserved for the native Linux implementation of Snippy.

## Planned Subsystems

1. **Screen Capture (`capture/`)**
   - Wayland: `org.freedesktop.portal.ScreenCast` via PipeWire buffer streaming.
   - X11 fallback: `XShmGetImage` / `XGetImage` for traditional X11 display servers.

2. **Overlay & Rendering (`overlay/`, `render/`)**
   - Wayland: `wlr-layer-shell-unstable-v1` overlay surface with Vulkan or Cairo/OpenGL rendering backend.
   - X11: Borderless override-redirect fullscreen window.

3. **Clipboard (`clipboard/`)**
   - Wayland: Data control protocol / `wl-copy`.
   - X11: `CLIPBOARD` atom selection.

4. **Global Shortcuts (`hotkey/`)**
   - Wayland: `org.freedesktop.portal.GlobalShortcuts`.
   - X11: `XGrabKey`.

5. **System Tray (`tray/`)**
   - `org.kde.StatusNotifierItem` D-Bus service.

6. **Settings & Storage (`settings/`)**
   - Stored in `$XDG_CONFIG_HOME/snippy/settings.json` (defaults to `~/.config/snippy/settings.json`) using `snippy::SerializeSettingsJson` and `snippy::DeserializeSettingsJson` from `core/settings.h`.
