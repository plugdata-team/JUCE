# Wayland Decision Log

## 1. 2026-06-23
- Decision: QA the Wayland backend against Weston, KDE (KWin) and GNOME (Mutter).
- Rationale: These cover the reference compositor plus the two desktops most users actually run.

## 2. 2026-06-24
- Decision: Build a smoke test plus a JUCE probe app to deploy to the QA VMs, reporting via
  `--report` and usable in the UI.
- Rationale: Gives a repeatable, scriptable way to validate the backend on each compositor instead
  of manual poking.

## 3. 2026-06-24
- Decision: No peer integration tests in CI.
- Rationale: No existing JUCE peer is integration tested, rely on my testing/QA harness internally.

## 4. 2026-06-25
- Decision: Defer making the final call on Wayland *only* builds (ie, no X11 at compile time) until
  we are in QA.
- Rationale: Most distros still have X11 around to compile against. However embedded/kiosk
  Yocto/Buildroot images could be Wayland-only / built without X11 in the sysroot at all.
  Being able to compile JUCE without dragging in libX11 could be a size/hygiene benefit there. We
  could ask AlphaTheta if we can't figure it out via the i.MX directly. It's trivial
  enough for me to convert a VM to be wayland-only.

## 5. 2026-06-25
- Decision: Hand-roll the `wl_*`/`xdg_*` types, `wl_interface`/`wl_message` tables, listeners,
  opcodes, and `WL_*` constants rather than use wayland-scanner codegen / plugdata's
  headers-plus-macro-redirect.
- Rationale: Avoids a new compile-time dependency (libwayland-dev), only have to manage one
  marshalling call for wayland >= 1.20 (`wl_proxy_marshal_flags`), similar feel to X11 code. Wayland
  protocol is append-only, future maintenance burden is manageable.

## 6. 2026-06-25
- Decision: Compile Wayland backend by default and make it the runtime default with X11 fallback.
- Rationale: Wayland is de facto available on almost all modern distros. Since we don't depend on it
  for compile and we dlopen at runtime, we can fall back to X11. Biggest "con" of this decision: most
  plugins would ship with an unused wayland backend.

## 7. 2026-06-26
- Decision: Build the Wayland backend on FreeBSD/BSD `juce_gui_basics` builds, not just Linux.
- Rationale: Treats Linux and BSD as the same native windowing family.

## 8. 2026-06-26
- Decision: Confine all `wl_proxy*` casts and raw `wl_proxy_*` calls to `juce::WaylandProtocol`.
  Peer and window-system code can only call our typed `WaylandProtocol` wrappers, never `wl_proxy*`
  directly.
- Rationale: Keeps the type-erased libwayland ABI behind one typed layer, mirroring the X11
  `XSymbols` split.

## 9. 2026-06-30
- Decision: ABI definitions live in a private `wayland/juce_WaylandProtocolTypes_linux.h`. The
  public protocol header should only have forward declarations.
- Rationale: Stops redefinition / ODR clashes if someone includes the real `<wayland-client.h>`. We
  hand-roll the ABI to build without libwayland dev headers, so those definitions must stay off
  the public header path.

## 10. 2026-06-30
- Decision: An X11 plugin editor should create `LinuxComponentPeer`, not `WaylandComponentPeer` when
  making secondary desktop windows (popups, tooltips, modals) vs. inherit their owner's backend.
- Rationale: Popups/tooltips/modals are created without a host parent handle. Just checking session
  would float native Wayland popups over an X11 editor. We need to make this per-window, not
  per-process: a Wayland host (AudioPluginHost) should be able to have a native main window with X11 plugin
  children.

## 11. 2026-07-01
- Decision: Repaint only when the compositor asks for another frame. Use that callback as JUCE’s
  VBlank tick. Use a Timer to clean up idle surfaces, as there may not be callbacks to clean up
  scratchImage. Coalesce repaints
- Rationale: Wayland expects apps to wait for the `wl_surface.frame` callback instead of repainting on
  their own timer (as X11 does). This avoids wasted redraws, such as when a window is hidden or
  covered.

## 12. 2026-07-01
- Decision: 2 buffers per surface (window), which are replaced when the surface size changes.
- Rationale: Lets each window/popup/tooltip repaint independently. We need two buffers, as the
  compositor might still be using the last frame while we prepare the next.

## 13. 2026-07-07
- Decision: Support XDG Desktop Portals and make a client that X11 and Wayland can both use.
  DBusSymbols, a dlopen table for libdbus-1.so.3 that lives in juce_core (so URL::launchInDefaultBrowser can
  use it) and a XDG Desktop Portal client that sits in juce_gui_basics.
- Rationale: By design, Wayland doesn't have global window ids and clients don't know anything
  about each other. The only way to have child/parent windows in a new process is by grabbing a
  window token via xdg-foreign and talking over D-Bus. For file dialogs, that's the only way to
  parent them to a JUCE window. Dialogs, screenshots, dark mode and opening URLs are also excluded
  from the protocol and happen via D-Bus. Relying on zenity for dialogs would mean they
  would be orphaned. X11 has usage for D-Bus as well, in sandboxed flatpak'd hosts.

## 14. 2026-07-08
- Decision: Extract shared translateKeySymToKeyPress helper so that Wayland can reuse the same
  keysym code as X11.
- Rationale: The alternative is duplication. It's mostly a bag o' constants and easily testable.

## 15. 2026-07-17
- Decision: Make Portals first class and default for X11.
- Rationale: Portals are the more modern platform-wide choice, with better parenting vs. zenity,
  etc.

## 16. 2026-07-21
- Decision: Bind wl_seat up to v10 when possible
- Rationale: The wl_seat version clamps the whole family's interface versioning (pointer/keyboard/touch) and
  the listener structs must then "cover" up to that max version. We want to support v8 of wl_pointer
  for `axis_value120` (hi-res mouse wheel / track pad, libwayland 1.21) and 9 for
  `axis_relative_direction` (reverse scrollwheel, libwayland 1.22), 10 for compositor-driven
  key-repeat (vs. client-side, libwayland 1.24, GNOME 49+ and Plasma 6.5). Weston reference
  compositor (iMX) only supports up to v7 as of Summer 2026.
- Revisit When: A mainstream compositor advertises v11 (wl_pointer.warp, libwayland 1.26)

## 17. 2026-07-21
- Decision: Feature-guard specific interface versions at the request site vs. enforce a wl_seat minimum
  version at bind time.
- Rationale: Interface version support is specified by compositors (the server) at runtime.
  Everything a JUCE peer needs exists at v1, old compositors just lose out on a handful of features.
  Feature detection is the more graceful path.
- Revisit When: There's more than a dozen call guard call sites and it feels cleaner to do something
  like refuse legacy compositors below vX at bind time.

## 18. 2026-07-29
- Decision: On a fatal wl_display error (compositor disconnect or protocol error), log the error via
  DBG, and when in Standalone, stop the message loop so the app quits.
- Rationale: Stopping the Standalone matches the X11 backend and other backends by not attempting to
  reconnect. Unregistering the fd prevents a disconnected compositor from causing the event loop to spin
  indefinitely. Protocol failures are not currently logged in X11.
- Revisit When: We want to log protocol errors in Release builds via Logger::writeToLog(). Or when
  reconnecting is considered to be desirable enough that it is worth the added complexity of a full
  re-bind/teardown of the Wayland stack (within a retry loop).

## 19. 2026-07-31
- Decision: Treat the first usable display as JUCE's primary display.
- Rationale: `Displays::Display::isMain` and `Displays::getPrimaryDisplay()` make primary display
  status part of JUCE's cross-platform model but Wayland does not identify a primary output. Mac/Win
  backends obtain it from the platform. X11 attempts via XRRGetOutputPrimary and falls back to the
  first display when necessary.
- Revisit When: A mainstream Wayland protocol with broad support exposes primary-output semantics.

## 20. 2026-08-05
- Decision: Bind `wl_compositor` up to v6 when available. Prefer
  `wp_fractional_scale_v1.preferred_scale`, then `wl_surface.preferred_buffer_scale`, and fall back
  to the scales of the outputs containing the surface.
- Rationale: v6 is 2026 mainstream (Mutter, etc) and can provide a better scale than JUCE can infer
  from `wl_output.scale`. Fractional scale remains the most precise signal when available.
- Revisit When: Optimising presentation on rotated/reflected outputs becomes worthwhile, or
  JUCE needs a feature from a future `wl_surface` version.

## 21. 2026-08-05
- Decision: Use `wl_pointer.set_cursor` with the first libwayland-cursor theme image
  for standard cursors. Do not do client-side animation of frames or implement
  `wp_cursor_shape_manager_v1` yet.
- Rationale: GNOME/Mutter and Weston do not yet implement cursor-shape. Client-side animation of
  cursors is unideal compared to compositor driven implementations.
- Revisit When: Cursor-shape is broadly supported or solves a demonstrated cursor problem.

## 22. 2026-08-12
- Decision: Use `libdecor` as a fallback for client-side GNOME/Mutter decorations when
  `useNativeTitleBar (true)`. Use `xdg-decoration` to detect when compositor/server-side decorations
  are preferred. Use XdgShell as the default top-level window decoration. Popup creation and
  placement is always done through xdg-shell.
- Rationale: Libdecor-only was considered, but nobody in the ecosystem does this. In addition,
  it would be a heavy/hard dependency for minimal distro / embedded cases. Correct display of
  decorations on libdecor requires compositor-specific plugins installed and `useNativeTitleBar
  (false)` becomes just a hint to the compositor vs. a requirement.
- Revisit When: Maintaining two top-levels is determined to be too costly or fragile.

## 23. 2026-08-21
- Decision: Use `eglSwapInterval (0)` and manually pace OpenGL surfaces with `wl_surface.frame`
  callbacks. Process callbacks through JUCE's existing Wayland event dispatch on the message thread.
  Each callback grants the render thread permission to draw one frame. Allow the first frame without
  a callback. JUCE only starts another frame when the compositor asks for one. If callbacks stop,
  rendering stops too, but the render thread remains available.
- Rationale: Wayland protocol recommends that the compositor avoid sending frame callbacks when a
  surface is hidden or completely obscured. When a surface is hidden and `eglSwapInterval` is set to
  1, `eglSwapBuffers` could remain blocked, preventing the OpenGL render thread from doing other
  work. JUCE needs that thread to remain available for worker jobs and context removal.
- Revisit When: Dispatching the frame callback through JUCE’s message thread causes issues such as
  frame pacing delays. In that case, consider dispatching it from the OpenGL render thread with a
  private Wayland event queue.

## 24. 2026-08-25
- Decision: Use `xdg_popup` for desktop peers with the `windowIsTemporary` flag. If no popup parent is
  available, fall back to `xdg_toplevel` for the lifetime of the peer.
- Rationale: Wayland does not let a client position an `xdg_toplevel` relative to one of its
  windows. JUCE relies on that positioning for menus, callouts, tooltips, bubbles, shadows, focus
  outlines, and drag images.
