/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
 */

namespace juce
{

struct WaylandShmBuffer {
    wl_buffer* buffer;
    void* data = nullptr;
    uint64 size = 0;
    int fd = -1;
    int width = 0;
    int height = 0;
    wl_shm_pool* pool;
    int hotspotX = 0;
    int hotspotY = 0;
    
    WaylandShmBuffer (int bufferWidth, int bufferHeight)
    {
        width = bufferWidth;
        height = bufferHeight;
        
        const uint64 stride = (uint64)bufferWidth * 4; // bytes per pixel
        size = stride * (uint64)bufferHeight;
        
        // Get a file descriptor to share
        fd = [] (uint64 bufsize){
#if JUCE_LINUX
            // Try memfd_create (Linux-specific)
            int memfd = syscall (SYS_memfd_create, "wayland-shm", MFD_CLOEXEC);
            if (memfd >= 0)
            {
                if (ftruncate (memfd, (off_t)bufsize) == 0)
                    return memfd;
                
                close (memfd);
            }
#endif
            
            char name[] = "/tmp/wayland-shm-XXXXXX";
            int tempfd = mkstemp (name);
            if (tempfd < 0)
                return -1;
            
            unlink (name);  // Remove file from filesystem but keep fd open
            
            if (ftruncate (tempfd, (off_t)bufsize) < 0)
            {
                close (tempfd);
                return -1;
            }
            return tempfd;
        }(size);
        
        if (fd < 0)
            throw std::runtime_error ("Failed to create shared memory file");
        
        // mmap the shared memory
        data = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED)
        {
            close (fd);
            fd = -1;
            throw std::runtime_error ("Failed to mmap shared memory");
        }
        
        // Create the wl_shm_pool using shared shm
        pool = WaylandSymbols::getInstance()->shmCreatePool (WaylandWindowSystem::getInstance()->getShm(), fd, size);
        
        // Create wl_buffer from pool
        buffer = WaylandSymbols::getInstance()->shmPoolCreateBuffer (pool, 0, bufferWidth, bufferHeight, stride, WL_SHM_FORMAT_ARGB8888);
    }
    
    ~WaylandShmBuffer()
    {
        if (buffer)
            WaylandSymbols::getInstance()->bufferDestroy (buffer);
        if (pool)
            WaylandSymbols::getInstance()->shmPoolDestroy (pool);
        if (data && data != MAP_FAILED)
            munmap (data, size);
        if (fd >= 0)
            close (fd);
    }
};

struct WaylandWindow {
    wl_surface* surface;
    union {
        libdecor_frame* frame = nullptr;
        wl_subsurface* subsurface;
    } handle;
    wl_callback* frameCallback = nullptr;
    WaylandWindow* parentWindow = nullptr;
    WaylandComponentPeer* peer;
    std::unique_ptr<WaylandShmBuffer> currentBuffer = nullptr;
    std::set<wl_output*> displays;
    Rectangle<int> bounds;
    uint16 bufferScale = 1;
    bool isVisible:1 = true;
    bool isFullscreen:1 = false;
    bool isMinimised:1 = false;
    bool isActivated:1 = false;
    bool ignoresMouse:1 = false;
    bool ignoresKeyboard:1 = false;
    bool isAlwaysOnTop:1 = false;
    bool isOpaque:1 = false;
};

struct WaylandDisplay {
    wl_output* output;
    Rectangle<int> bounds;
    int physicalWidth = 0, physicalHeight = 0;
    int refreshRate = 0;
    int scaleFactor = 1;
    bool isDone:1 = false;
    bool isPrimary:1 = false;
};

WaylandWindowSystem::WaylandWindowSystem()
{
    if (initialised) return;
    
    auto* currentApp =  JUCEApplicationBase::getInstance();
    // Check if we're running under Wayland, or if the user prefers XWayland
    if (! std::getenv ("WAYLAND_DISPLAY") || std::getenv ("JUCE_XWAYLAND") || ! currentApp)
        return;
    
    if (! WaylandSymbols::getInstance()->loadAllSymbols())
        return;
    
    // initialise XKB context for this window
    xkbContext = WaylandSymbols::getInstance()->xkbContextNew (XKB_CONTEXT_NO_FLAGS);
    if (! xkbContext)
        return;
    
    display = WaylandSymbols::getInstance()->displayConnect (nullptr);
    if (! display)
        return;
    
    registry = WaylandSymbols::getInstance()->displayGetRegistry (display);
    if (! registry)
        return;
    
    static const wl_registry_listener registryListener =
    {
        .global = [] (void* data, wl_registry* r, uint32 name, const char* interfaceName, uint32 version)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            auto interface = String::fromUTF8 (interfaceName);
            if (interface == "wl_compositor")
            {
                self->compositor = static_cast<wl_compositor*>(WaylandSymbols::getInstance()->registryBind (r, name, WaylandSymbols::getInstance()->compositorInterface, std::min (version, 4u)));
            }
            else if (interface == "wl_subcompositor")
            {
                self->subcompositor = static_cast<wl_subcompositor*> (WaylandSymbols::getInstance()->registryBind (r, name, WaylandSymbols::getInstance()->subcompositorInterface, version));
            }
            else if (interface == "wl_shm")
            {
                self->shm = static_cast<wl_shm*>(WaylandSymbols::getInstance()->registryBind (r, name, WaylandSymbols::getInstance()->shmInterface, std::min (version, 1u)));
            }
            else if (interface =="wl_seat")
            {
                self->seat = static_cast<wl_seat*>(WaylandSymbols::getInstance()->registryBind (r, name, WaylandSymbols::getInstance()->seatInterface, std::min (version, 7u)));
                
                
                static const wl_seat_listener seatListener =
                {
                    .capabilities = [] (void* d, wl_seat*, uint32 capabilities)
                    {
                        auto* s = static_cast<WaylandWindowSystem*> (d);
                        s->hasTouch = capabilities & WL_SEAT_CAPABILITY_TOUCH;
                        s->hasMouse = capabilities & WL_SEAT_CAPABILITY_POINTER;
                        s->hasKeyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
                        s->setupGlobalInput();
                    },
                    .name = [] (void*, wl_seat*, const char*) {}
                };
                
                WaylandSymbols::getInstance()->seatAddListener (self->seat, &seatListener, self);
            }
            else if (interface == "wl_data_device_manager")
            {
                self->dataDeviceManager = static_cast<wl_data_device_manager*> (WaylandSymbols::getInstance()->registryBind (r, name, WaylandSymbols::getInstance()->dataDeviceManagerInterface, version));
            }
            else if (interface == "wl_output")
            {
                wl_output* output = static_cast<wl_output*> (WaylandSymbols::getInstance()->registryBind (r, name, WaylandSymbols::getInstance()->outputInterface, std::min (version, 4u)));
                self->setupDisplay (output);
            }
        },
        .global_remove = [] (void*, wl_registry*, uint32) {}
    };
    
    WaylandSymbols::getInstance()->registryAddListener (registry, &registryListener, this);
    WaylandSymbols::getInstance()->displayRoundtrip (display);
    
    // Check for required globals
    String missing_globals;
    if (! compositor)        missing_globals += "wl_compositor ";
    if (! subcompositor)     missing_globals += "wl_subcompositor ";
    if (! shm)               missing_globals += "wl_shm ";
    if (! seat)              missing_globals += "wl_seat ";
    if (! dataDeviceManager) missing_globals += "wl_data_device_manager ";
    if (missing_globals.isNotEmpty())
    {
        std::cerr << "Missing required Wayland globals: " << missing_globals << std::endl;
        return;
    }
    
    static const libdecor_interface decorInterface =
    {
        .error = [] (libdecor*, enum libdecor_error error, const char* message)
        {
            std::cerr << "Libdecor error " << error << ": " << message << std::endl;
        }
    };
    decorator = WaylandSymbols::getInstance()->decorNew (display, &decorInterface);
    
    setupDataDeviceCallbacks();
    
    // Register the event loop callback
    LinuxEventLoop::registerFdCallback (WaylandSymbols::getInstance()->displayGetFd (display), [](int) mutable {});
    
    initialised = true;
}

WaylandWindowSystem::~WaylandWindowSystem()
{
    customCursors.clear();
    if (cursorTheme)
        WaylandSymbols::getInstance()->cursorThemeDestroy (cursorTheme);
    
    if (decorator)
        WaylandSymbols::getInstance()->decorUnref (decorator);
    
    if (display)
        WaylandSymbols::getInstance()->displayDisconnect (display);
    
    WaylandSymbols::deleteInstance();
    clearSingletonInstance();
}

WaylandWindow* WaylandWindowSystem::createWindow (bool isSubsurface, ComponentPeer* peer, bool alwaysOnTop, WaylandWindow* parent)
{
    auto* window = new WaylandWindow();
    window->peer = dynamic_cast<WaylandComponentPeer*> (peer);
    if (isSubsurface && ! parent)
    {
        window->parentWindow = lastFocusedWindow;
        if (! window->parentWindow)
            isSubsurface = false; // fall back to regular window if we have no parent to attach to
    }
    else
    {
        window->parentWindow = parent;
    }
    
    window->surface = WaylandSymbols::getInstance()->compositorCreateSurface (getCompositor());
    window->ignoresKeyboard = (peer->getStyleFlags() & ComponentPeer::windowIgnoresKeyPresses);
    window->ignoresMouse = (peer->getStyleFlags() & ComponentPeer::windowIgnoresMouseClicks);
    window->isOpaque = ! (peer->getStyleFlags() & ComponentPeer::windowIsSemiTransparent);
    window->isAlwaysOnTop = alwaysOnTop;
    
    static const struct wl_surface_listener surfaceListener = {
        .enter = [](void* data, struct wl_surface*, struct wl_output* output){
          auto* w = static_cast<WaylandWindow*> (data);
          w->displays.insert (output);
          
          // If our window entered a new display, update scale
          w->bufferScale = getInstance()->getScaleFactorForWindow (w);
          WaylandSymbols::getInstance()->surfaceSetBufferScale (w->surface, w->bufferScale);
          
          if (ComponentPeer::isValidPeer (w->peer))
            dynamic_cast<WaylandComponentPeer*> (w->peer)->updateScaleFactor();
        },
        .leave = [](void* data, struct wl_surface*, struct wl_output* output){
          auto* w = static_cast<WaylandWindow*> (data);
          w->displays.erase (output);
        }
    };
    WaylandSymbols::getInstance()->surfaceAddListener (window->surface, &surfaceListener, window);
    
    updateRegions (window);
    
    static bool isConfigured;
    isConfigured = false;

    if (isSubsurface)
    {
        window->handle.subsurface = WaylandSymbols::getInstance()->subcompositorGetSubsurface (subcompositor, window->surface, window->parentWindow->surface);
        WaylandSymbols::getInstance()->subsurfaceSetPosition (window->handle.subsurface, 0, 0);
        WaylandSymbols::getInstance()->subsurfaceSetDesync (window->handle.subsurface);
        
        window->bufferScale = getInstance()->getScaleFactorForWindow (window);
        WaylandSymbols::getInstance()->surfaceSetBufferScale (window->surface, window->bufferScale);
        
        WaylandSymbols::getInstance()->surfaceCommit (window->surface);
        WaylandSymbols::getInstance()->surfaceCommit (window->parentWindow->surface);
    }
    else
    {
        auto styleFlags = peer->getStyleFlags();
        
        int capabilities = LIBDECOR_ACTION_MOVE;
        if (styleFlags & ComponentPeer::windowHasMinimiseButton)
            capabilities |= (int)LIBDECOR_ACTION_MINIMIZE;
        if (styleFlags & ComponentPeer::windowHasMaximiseButton)
            capabilities |= (int)LIBDECOR_ACTION_FULLSCREEN;
        if (styleFlags & ComponentPeer::windowHasCloseButton)
            capabilities |= (int)LIBDECOR_ACTION_CLOSE;
        if (styleFlags & ComponentPeer::windowIsResizable)
            capabilities |= (int)LIBDECOR_ACTION_RESIZE;
        
        static const libdecor_frame_interface frame_interface =
        {
            .configure = [] (libdecor_frame* frame, libdecor_configuration* configuration, void* data)
            {
                auto* w = static_cast<WaylandWindow*> (data);
                if (! ComponentPeer::isValidPeer (w->peer))
                {
                    auto* state = WaylandSymbols::getInstance()->decorStateNew (1, 1);
                    WaylandSymbols::getInstance()->decorFrameCommit (frame, state, configuration);
                    if (state) WaylandSymbols::getInstance()->decorStateFree (state);
                    return;
                }
                bool wasActivated = w->isActivated;

                int windowState;
                if (WaylandSymbols::getInstance()->decorConfigurationGetWindowState (configuration, &windowState))
                {
                    w->isFullscreen = windowState & (LIBDECOR_WINDOW_STATE_TILED_LEFT  |
                                                     LIBDECOR_WINDOW_STATE_TILED_RIGHT |
                                                     LIBDECOR_WINDOW_STATE_TILED_TOP   |
                                                     LIBDECOR_WINDOW_STATE_TILED_BOTTOM |
                                                     LIBDECOR_WINDOW_STATE_MAXIMIZED);
                    w->isActivated = windowState & LIBDECOR_WINDOW_STATE_ACTIVE;
                    if (w->isActivated) w->isMinimised = false;
                }
                
                WaylandComponentPeer::isActiveApplication = false;
                for(auto& [surface, window] : WaylandWindowSystem::getInstance()->surfaceToWindow)
                {
                    if(window->isActivated)
                    {
                        WaylandComponentPeer::isActiveApplication = true;
                        break;
                    }
                }
                
                if (! wasActivated && w->isActivated)
                {
                    w->peer->handleBroughtToFront();
                }
                
                int width = std::max (1, (int)(w->peer->getComponent().getWidth()));
                int height = std::max (1, (int)(w->peer->getComponent().getHeight()));
                if (WaylandSymbols::getInstance()->decorConfigurationGetContentSize (configuration, frame, &width, &height))
                {
                    if (width > 0 && height > 0)
                    {
                        w->bounds.setSize (width, height);
                        w->peer->setBounds (Rectangle<int>(0, 0, width, height), w->isFullscreen);
                        w->peer->repaint (Rectangle<int>(0, 0, width, height));
                    }
                }
                
                auto* state = WaylandSymbols::getInstance()->decorStateNew (width, height);
                WaylandSymbols::getInstance()->decorFrameCommit (frame, state, configuration);
                WaylandSymbols::getInstance()->decorStateFree (state);

                getInstance()->updateRegions (w);
                isConfigured = true;
            },
                .close = [] (libdecor_frame*, void* user_data){
                    auto* w = static_cast<WaylandWindow*> (user_data);
                    MessageManager::callAsync ([peer = w->peer](){
                        if (ComponentPeer::isValidPeer (peer))
                            peer->handleUserClosingWindow();
                    });
                },
                .commit = [] (libdecor_frame*, void*) {},
        };
        
        
        window->handle.frame = WaylandSymbols::getInstance()->decorDecorate (decorator, window->surface, &frame_interface, window);
        WaylandSymbols::getInstance()->decorFrameSetCapabilities (window->handle.frame, (libdecor_capabilities)capabilities);
        
        if (! window->handle.frame)
        {
            jassertfalse; // Critical error, could not create window frame
            delete window;
            return nullptr;
        }
        
        WaylandSymbols::getInstance()->decorFrameMap (window->handle.frame);
        WaylandSymbols::getInstance()->surfaceCommit (window->surface);
        
        // Wait for initial configure
        int timeout = 50;
        while (! isConfigured && timeout-- > 0)
        {
            WaylandSymbols::getInstance()->displayDispatchPending (display);
            if (! isConfigured)
            {
                usleep (10000);
                WaylandSymbols::getInstance()->displayDispatch (display);
            }
        }
        
        if (! (styleFlags & ComponentPeer::windowHasTitleBar))
            WaylandSymbols::getInstance()->decorFrameSetVisibility (window->handle.frame, false);
    }
    
    if (! lastFocusedWindow)
        lastFocusedWindow = (window->parentWindow || ! window->isVisible) ? lastFocusedWindow : window;
    
    surfaceToWindow[window->surface] = window;
    
    toFront (window, false);
    return window;
}

WaylandWindow* WaylandWindowSystem::getWaylandWindowForPeer (ComponentPeer* peer)
{
    if (auto* waylandPeer = dynamic_cast<WaylandComponentPeer*> (peer))
        return waylandPeer->getWindow();
    
    return nullptr;
}

int WaylandWindowSystem::getScaleFactorForWindow (WaylandWindow* window)
{
    for (auto const& d : displays)
        if(window->displays.find (d.output) != window->displays.end())
          return d.scaleFactor;
    
    return displays.size() ? displays[0].scaleFactor : 1;
}

void WaylandWindowSystem::requestFrame (WaylandWindow* window)
{
    if (! window || ! window->surface || window->frameCallback)
        return;
    
    static const wl_callback_listener frameListener = {
        .done = [] (void* data, wl_callback* callback, uint32)
        {
            auto* w = static_cast<WaylandWindow*> (data);
            if (callback)
            {
                WaylandSymbols::getInstance()->callbackDestroy (callback);
                w->frameCallback = nullptr;
                
                // Trigger repaint if window is still valid
                if (ComponentPeer::isValidPeer (w->peer))
                    w->peer->onFrame();
            }
        }
    };
    
    window->frameCallback = WaylandSymbols::getInstance()->surfaceFrame (window->surface);
    WaylandSymbols::getInstance()->callbackAddListener (window->frameCallback, &frameListener, window);
}

void WaylandWindowSystem::updateRegions (WaylandWindow* window) const
{
    if (window->ignoresMouse || ! window->isVisible)
    {
        auto emptyRegion = WaylandSymbols::getInstance()->compositorCreateRegion (compositor);
        WaylandSymbols::getInstance()->surfaceSetInputRegion (window->surface, emptyRegion);
        WaylandSymbols::getInstance()->regionDestroy (emptyRegion);
    }
    else
    {
        auto inputRegion = WaylandSymbols::getInstance()->compositorCreateRegion (compositor);
        WaylandSymbols::getInstance()->regionAdd (inputRegion, 0, 0, window->bounds.getWidth(), window->bounds.getHeight());
        WaylandSymbols::getInstance()->surfaceSetInputRegion (window->surface, inputRegion);
        WaylandSymbols::getInstance()->regionDestroy (inputRegion);
    }
    if (window->isOpaque)
    {
        auto opaqueRegion = WaylandSymbols::getInstance()->compositorCreateRegion (compositor);
        WaylandSymbols::getInstance()->regionAdd (opaqueRegion, 0, 0, window->bounds.getWidth(), window->bounds.getHeight());
        WaylandSymbols::getInstance()->surfaceSetOpaqueRegion (window->surface, opaqueRegion);
        WaylandSymbols::getInstance()->regionDestroy (opaqueRegion);
    }
}

void WaylandWindowSystem::updateConstraints (WaylandWindow* window) const
{
    if (window->parentWindow)
        return;
    
    auto globalScale = Desktop::getInstance().getGlobalScaleFactor();
    if ((window->peer->getStyleFlags() & ComponentPeer::windowIsResizable) == 0)
    {
        // Non-resizable window: set min/max to current size
        const auto bounds = window->peer->getBounds() * globalScale;
        WaylandSymbols::getInstance()->decorFrameSetMinContentSize (window->handle.frame, bounds.getWidth(), bounds.getHeight());
        WaylandSymbols::getInstance()->decorFrameSetMaxContentSize (window->handle.frame, bounds.getWidth(), bounds.getHeight());
    }
    else if (auto* c = window->peer->getConstrainer())
    {
        // Resizable window with constraints
        const int minWidth  = jmax (1, (int)c->getMinimumWidth()) * globalScale;
        const int maxWidth  = jmax (1, (int)c->getMaximumWidth()) * globalScale;
        const int minHeight = jmax (1, (int)c->getMinimumHeight()) * globalScale;
        const int maxHeight = jmax (1, (int)c->getMaximumHeight()) * globalScale;
        
        WaylandSymbols::getInstance()->decorFrameSetMinContentSize (window->handle.frame, minWidth, minHeight);
        WaylandSymbols::getInstance()->decorFrameSetMaxContentSize (window->handle.frame, maxWidth, maxHeight);
    }
    else
    {
        // Resizable with no constraints - remove all limits
        WaylandSymbols::getInstance()->decorFrameSetMinContentSize (window->handle.frame, 1, 1);
        WaylandSymbols::getInstance()->decorFrameSetMaxContentSize (window->handle.frame, 0, 0);
    }
    
    // Commit the frame to apply changes immediately
    WaylandSymbols::getInstance()->decorFrameCommit (window->handle.frame, nullptr, nullptr);
}

void WaylandWindowSystem::destroyWindow (WaylandWindow* window)
{
    if (hasTouch)
        currentTouches->deleteAllTouchesForPeer (window->peer);
    
    auto it = findInZOrder (window);
    if (it != zOrder.end())
        zOrder.erase (it);
    
    // In case we delete the parent window before the child window, remove it from z-order
    // There might be more potential crashes that can be caused by deleting a parent window, but this is one of them
    for(auto& [surface, childWindow] : surfaceToWindow)
    {
        if(childWindow->parentWindow == window)
        {
            auto it = findInZOrder (childWindow);
            if (it != zOrder.end())
              zOrder.erase (it);
        }
    }

    if (window->frameCallback)
        WaylandSymbols::getInstance()->callbackDestroy (window->frameCallback);
    
    if (window->parentWindow)
        WaylandSymbols::getInstance()->subsurfaceDestroy (window->handle.subsurface);
    else
        WaylandSymbols::getInstance()->decorFrameUnref (window->handle.frame);
    
    if (window->surface)
    {
        auto windowIter = surfaceToWindow.find (window->surface);
        if (windowIter != surfaceToWindow.end())
        {
            surfaceToWindow.erase (windowIter);
            // Clear focus if this window had it
            if (keyboardFocused == window) keyboardFocused = nullptr;
            if (pointerFocused == window) pointerFocused = nullptr;
            if (lastFocusedWindow == window) lastFocusedWindow = nullptr;
        }
        
        WaylandSymbols::getInstance()->surfaceDestroy (window->surface);
    }
    
    delete window;
}

void WaylandWindowSystem::setBounds (WaylandWindow* window, Rectangle<int> b)
{
    if (b == window->bounds)
        return;
    
    if (window->parentWindow)
    {
        window->bounds = b;
        WaylandSymbols::getInstance()->subsurfaceSetPosition (window->handle.subsurface, window->bounds.getX(), window->bounds.getY());
        WaylandSymbols::getInstance()->surfaceCommit (window->surface);
        WaylandSymbols::getInstance()->surfaceCommit (window->parentWindow->surface);
    }
    else
    {
        window->bounds = b.withZeroOrigin();
        auto* state = WaylandSymbols::getInstance()->decorStateNew (b.getWidth(), b.getHeight());
        WaylandSymbols::getInstance()->decorFrameCommit (window->handle.frame, state, nullptr);
    }
}

Rectangle<int> WaylandWindowSystem::getBounds (WaylandWindow* window)
{
    return window->bounds;
}

void WaylandWindowSystem::startHostManagedResize (WaylandWindow* window, ResizableBorderComponent::Zone zone)
{
    if (window->parentWindow)
        return;
    
    auto edge = [zone]
    {
        using F = ResizableBorderComponent::Zone::Zones;
        switch (zone.getZoneFlags())
        {
            case F::top | F::left:      return LIBDECOR_RESIZE_EDGE_TOP_LEFT;
            case F::top:                return LIBDECOR_RESIZE_EDGE_TOP;
            case F::top | F::right:     return LIBDECOR_RESIZE_EDGE_TOP_RIGHT;
            case F::right:              return LIBDECOR_RESIZE_EDGE_RIGHT;
            case F::bottom | F::right:  return LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT;
            case F::bottom:             return LIBDECOR_RESIZE_EDGE_BOTTOM;
            case F::bottom | F::left:   return LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT;
            case F::left:               return LIBDECOR_RESIZE_EDGE_LEFT;
            default:
                break;
        }
        return LIBDECOR_RESIZE_EDGE_NONE;
    }();
    
    if (edge == LIBDECOR_RESIZE_EDGE_NONE)
        WaylandSymbols::getInstance()->decorFrameMove (window->handle.frame, seat, currentMouseSerial);
    else
        WaylandSymbols::getInstance()->decorFrameResize (window->handle.frame, seat, currentMouseSerial, edge);
}

void WaylandWindowSystem::toFront (WaylandWindow* window, bool commit)
{
    auto it = findInZOrder (window);
    if (it != zOrder.end())
        zOrder.erase (it);
    
    if (window->isAlwaysOnTop)
    {
        zOrder.push_back (window);
    }
    else {
        bool inserted = false;
        for (uint64 i = 0; i < zOrder.size(); i++)
        {
            if (zOrder[i]->isAlwaysOnTop)
            {
                zOrder.insert (zOrder.begin() + (int64)i, window);
                inserted = true;
                break;
            }
        }
        
        if (! inserted)
            zOrder.push_back (window);
    }
        
    if (commit)
        enforceOrder();
}

void WaylandWindowSystem::toBehind (WaylandWindow* window, WaylandWindow* referenceWindow)
{
    if (! window->parentWindow || window->parentWindow != referenceWindow->parentWindow)
        return; // Can only reorder subsurfaces with same parent
    
    auto windowIt = findInZOrder (window);
    auto refIt = findInZOrder (referenceWindow);
    
    if (windowIt == zOrder.end() || refIt == zOrder.end()) return;
    
    // Store indices before any modifications
    auto windowIndex = std::distance (zOrder.begin(), windowIt);
    auto refIndex = std::distance (zOrder.begin(), refIt);

    // Remove window from current position
    zOrder.erase (zOrder.begin() + windowIndex);
    
    // Adjust reference index if it was after the removed window
    if (refIndex > windowIndex)
        refIndex--;
    
    // Insert before the reference window
    zOrder.insert (zOrder.begin() + refIndex, window);
    WaylandSymbols::getInstance()->subsurfacePlaceBelow (window->handle.subsurface, referenceWindow->surface);
    WaylandSymbols::getInstance()->surfaceCommit (window->surface);
    WaylandSymbols::getInstance()->surfaceCommit (referenceWindow->surface);
}


// Wayland can't move windows "to front", it can only move it "just above" another windows,
// as long as they are part of the same parent structure
// So we need to keep track z-order manually, and enforce that order whenever needed by going down the chain
// and placing each window "just above" the last.
void WaylandWindowSystem::enforceOrder()
{
    std::map<WaylandWindow*, std::vector<WaylandWindow*>> windowsByParent;
    for (WaylandWindow* window : zOrder)
    {
        if (window->parentWindow)
            windowsByParent[window->parentWindow].push_back (window);
        else
            windowsByParent[window].push_back (window);
    }
    
    for (auto& [parent, children] : windowsByParent)
    {
        if (children.empty())
            continue;
        
        // First, split subsurfaces into a list of subsurfaces above and below the parent
        std::vector<WaylandWindow*> subsurfacesAbove;
        std::vector<WaylandWindow*> subsurfacesBelow;
        bool aboveParent = false;
        for (auto* child : children)
        {
            if (child == parent)
            {
                aboveParent = true;
            }
            else if (child->handle.subsurface && child->parentWindow == parent)
            {
                if (aboveParent)
                    subsurfacesAbove.push_back (child);
                else
                    subsurfacesBelow.push_back (child);
            }
        }
        
        // Stack subsurfaces below the parent
        if (! subsurfacesBelow.empty())
        {
            WaylandSymbols::getInstance()->subsurfacePlaceBelow (
                subsurfacesBelow[0]->handle.subsurface,
                parent->surface);
            
            for (size_t i = 1; i < subsurfacesBelow.size(); ++i)
            {
                WaylandSymbols::getInstance()->subsurfacePlaceAbove (
                    subsurfacesBelow[i]->handle.subsurface,
                    subsurfacesBelow[i-1]->surface);
            }
        }

        // Stack subsurfaces above the parent
        if (! subsurfacesAbove.empty())
        {
            WaylandSymbols::getInstance()->subsurfacePlaceAbove (
                subsurfacesAbove[0]->handle.subsurface,
                parent->surface);

            for (size_t i = 1; i < subsurfacesAbove.size(); ++i)
            {
                WaylandSymbols::getInstance()->subsurfacePlaceAbove (
                    subsurfacesAbove[i]->handle.subsurface,
                    subsurfacesAbove[i-1]->surface);
            }
        }
        
        for (auto* s : subsurfacesBelow)
            WaylandSymbols::getInstance()->surfaceCommit (s->surface);

        WaylandSymbols::getInstance()->surfaceCommit (parent->surface);

        for (auto* s : subsurfacesAbove)
            WaylandSymbols::getInstance()->surfaceCommit (s->surface);
    }
}

void WaylandWindowSystem::setVisible (WaylandWindow* window, bool shouldBeVisible)
{
    window->isVisible = shouldBeVisible;
    if (shouldBeVisible)
    {
        if (window->currentBuffer)
        {
            WaylandSymbols::getInstance()->surfaceAttach (window->surface, window->currentBuffer->buffer, 0, 0);
            WaylandSymbols::getInstance()->surfaceCommit (window->surface);
        }
        requestFrame (window);
        if (window->parentWindow)
        {
            WaylandSymbols::getInstance()->surfaceCommit (window->parentWindow->surface);
        }
    }
    else
    {
        WaylandSymbols::getInstance()->surfaceAttach (window->surface, nullptr, 0, 0);
        WaylandSymbols::getInstance()->surfaceCommit (window->surface);
        if (window->parentWindow)
        {
            WaylandSymbols::getInstance()->surfaceCommit (window->parentWindow->surface);
        }
    }
    updateRegions (window);
}

void WaylandWindowSystem::setFullscreen (WaylandWindow* window, bool shouldBeFullscreen)
{
    if (! window->parentWindow)
    {
        if (shouldBeFullscreen)
            WaylandSymbols::getInstance()->decorFrameSetMaximized (window->handle.frame);
        else
            WaylandSymbols::getInstance()->decorFrameUnsetMaximized (window->handle.frame);
    }
    window->isFullscreen = shouldBeFullscreen;
}

void WaylandWindowSystem::setMinimised (WaylandWindow* window, bool shouldBeMinimised)
{
    // Only supported for top-level windows
    if (shouldBeMinimised && window->handle.frame)
    {
        window->isMinimised = true;
        WaylandSymbols::getInstance()->decorFrameSetMinimized (window->handle.frame);
    }
}

bool WaylandWindowSystem::isFullscreen (WaylandWindow* window)
{
    return window->isFullscreen;
}

bool WaylandWindowSystem::isMinimised (WaylandWindow* window)
{
    return window->isMinimised;
}

void WaylandWindowSystem::grabFocus (WaylandWindow* window)
{
    keyboardFocused = window->ignoresKeyboard ? keyboardFocused : window;
}

bool WaylandWindowSystem::isFocused (WaylandWindow* window)
{
    return pointerFocused == window || keyboardFocused == window;
}

void WaylandWindowSystem::blitToWindow (WaylandWindow* window, const Image& image, Rectangle<int> bounds, Rectangle<int> totalArea)
{
    auto bufferScale = window->bufferScale;
    int windowWidth = window->bounds.getWidth() * bufferScale;
    int windowHeight = window->bounds.getHeight() * bufferScale;
        
    if (windowWidth <= 0 || windowHeight <= 0 || ! window->isVisible)
        return;
    
    auto& currentBuffer = window->currentBuffer;
    if (! currentBuffer || currentBuffer->width != windowWidth || currentBuffer->height != windowHeight)
    {
        try
        {
            currentBuffer = std::make_unique<WaylandShmBuffer> (windowWidth, windowHeight);
            WaylandSymbols::getInstance()->surfaceAttach (window->surface, currentBuffer->buffer, 0, 0);
        }
        catch (...)
        {
            std::cerr << "Failed to create SHM buffer" << std::endl;
            return;
        }
    }
    
    bounds = bounds.getIntersection (Rectangle<int>(0, 0, windowWidth, windowHeight));
    
    uint32* bufferData = static_cast<uint32*> (currentBuffer->data);
    const Image::BitmapData srcData (image, Image::BitmapData::readOnly);
    
    const Point<int> srcOffset = bounds.getPosition() - totalArea.getPosition();
    for (int y = 0; y < bounds.getHeight(); ++y)
    {
        uint32* srcPixel = reinterpret_cast<uint32*> (srcData.getLinePointer (srcOffset.y + y)) + srcOffset.x;
        uint32* destPixel = bufferData + ((bounds.getY() + y) * windowWidth) + bounds.getX();
        std::memcpy (destPixel, srcPixel, (uint64)bounds.getWidth() * sizeof (uint32));
    }
    
    WaylandSymbols::getInstance()->surfaceDamageBuffer (window->surface, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight());
}

void WaylandWindowSystem::setTitle (WaylandWindow* window, const String& title)
{
    // Only toplevel windows have titles
    if (! window->parentWindow)
        WaylandSymbols::getInstance()->decorFrameSetTitle (window->handle.frame, title.toRawUTF8());
}

wl_surface* WaylandWindowSystem::getSurfaceForWindow (WaylandWindow* window)
{
    return window->surface;
}

WaylandWindow* WaylandWindowSystem::findWindowBySurface (wl_surface* surface)
{
    auto it = surfaceToWindow.find (surface);
    return (it != surfaceToWindow.end()) ? it->second : nullptr;
}

void WaylandWindowSystem::updateMouseModifiers (uint32 button, bool pressed)
{
    auto& mods = ModifierKeys::currentModifiers;
    switch (button)
    {
        case BTN_LEFT:
            if (pressed) mods = mods.withFlags (ModifierKeys::leftButtonModifier);
            else         mods = mods.withoutFlags (ModifierKeys::leftButtonModifier);
            break;
        case BTN_RIGHT:
            if (pressed) mods = mods.withFlags (ModifierKeys::rightButtonModifier);
            else         mods = mods.withoutFlags (ModifierKeys::rightButtonModifier);
            break;
        case BTN_MIDDLE:
            if (pressed) mods = mods.withFlags (ModifierKeys::middleButtonModifier);
            else         mods = mods.withoutFlags (ModifierKeys::middleButtonModifier);
            break;
        default:
            break;
    };
}

ModifierKeys WaylandWindowSystem::getNativeRealtimeModifiers()
{
    auto mods = ModifierKeys::currentModifiers.withOnlyMouseButtons();
    if (xkbState != nullptr)
    {
        const auto active = XKB_STATE_MODS_EFFECTIVE;
        
        if (WaylandSymbols::getInstance()->xkbStateModNameIsActive (xkbState, XKB_MOD_NAME_SHIFT, active))
            mods = mods.withFlags (ModifierKeys::shiftModifier);
    
        if (WaylandSymbols::getInstance()->xkbStateModNameIsActive (xkbState, XKB_MOD_NAME_CTRL, active))
            mods = mods.withFlags (ModifierKeys::ctrlModifier);
        
        if (WaylandSymbols::getInstance()->xkbStateModNameIsActive (xkbState, XKB_MOD_NAME_ALT, active))
            mods = mods.withFlags (ModifierKeys::altModifier);
        
        if (WaylandSymbols::getInstance()->xkbStateModNameIsActive (xkbState, XKB_MOD_NAME_LOGO, active))
            mods = mods.withFlags (ModifierKeys::commandModifier); // Maps to Cmd on mac, Win key on Linux
    }
    return mods;
}

Point<float> WaylandWindowSystem::getCurrentMousePosition()
{
    return currentMousePosition;
}

bool WaylandWindowSystem::isWaylandAvailable()
{
    return initialised;
}

bool WaylandWindowSystem::isTouchAvailable()
{
  return hasTouch;
}

void WaylandWindowSystem::setupGlobalInput()
{
    if (! seat)
        return;
    
    // Keyboard setup
    static const wl_keyboard_listener keyboardListener =
    {
        .keymap = [] (void* data, wl_keyboard*, uint32, int32 fd, uint32 size)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            self->setupKeymap (fd, size);
        },
        .enter = [] (void* data, wl_keyboard*, uint32, wl_surface* surface, wl_array*)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->findWindowBySurface (surface))
            {
                if (! window->ignoresKeyboard) {
                    WaylandComponentPeer::isActiveApplication = true;
                    self->keyboardFocused = window;
                    window->peer->handleFocusGain();
                }
                if (! self->lastFocusedWindow) self->lastFocusedWindow = (window->parentWindow || ! window->isVisible) ? self->lastFocusedWindow : window;
            }
        },
        .leave = [] (void* data, wl_keyboard*, uint32, wl_surface* surface)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            WaylandWindow* window = self->findWindowBySurface (surface);
            if (! self->keysCurrentlyDown.empty())
            {
                self->keysCurrentlyDown.clear();
                if (self->keyboardFocused)
                    self->keyboardFocused->peer->handleKeyUpOrDown (false);
            }
            self->keyRepeater.stopTimer();
            if (window && self->keyboardFocused == window) {
                self->keyboardFocused = nullptr;
                window->peer->handleFocusLoss();
            }
        },
        .key = [] (void* data, wl_keyboard*, uint32, uint32, uint32 key, uint32 state)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->keyboardFocused)
                self->handleKeyEvent (window, key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
        },
        .modifiers = [] (void* data, wl_keyboard*, uint32, uint32 depressed, uint32 latched, uint32 locked, uint32 group)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->keyboardFocused)
            {
                if (self->xkbState)
                    WaylandSymbols::getInstance()->xkbStateUpdateMask (self->xkbState, depressed, latched, locked, 0, 0, group);
                
                ModifierKeys::currentModifiers = self->getNativeRealtimeModifiers();
                window->peer->handleModifierKeysChange();
                window->peer->handleKeyUpOrDown (depressed);
            }
        },
        .repeat_info = [] (void* data, wl_keyboard*, int32 rate, int32 delay)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            self->keyRepeatRate = rate;
            self->keyRepeatDelay = delay;
        }
    };
    
    // Pointer setup
    static const wl_pointer_listener pointerListener =
    {
        .enter = [] (void* data, wl_pointer*, uint32 serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->findWindowBySurface (surface))
            {
                self->currentMousePosition = window->peer->localToGlobal (WaylandSymbols::getInstance()->fixedToPoint<float>(x, y));
                self->currentMouseSerial = serial;
                self->updateCursor();
                
                if (! window->ignoresMouse)
                    self->pointerFocused = window;
                self->lastFocusedWindow = (window->parentWindow || ! window->isVisible) ? self->lastFocusedWindow : window;
                self->handleMouseEvent (window);
            }
        },
        .leave = [] (void* data, wl_pointer*, uint32, wl_surface* surface)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);

            auto mods = ModifierKeys::currentModifiers;
            // Send mouse-up events for any held mouse buttons
            if (mods.isLeftButtonDown())
            {
                self->updateMouseModifiers (BTN_LEFT, false);
                if (auto* window = self->findWindowBySurface (surface))
                  self->handleMouseEvent (window);
            }
            if (mods.isRightButtonDown())
            {
                self->updateMouseModifiers (BTN_RIGHT, false);
                if (auto* window = self->findWindowBySurface (surface))
                  self->handleMouseEvent (window);
            }
            if (mods.isMiddleButtonDown())
            {
                self->updateMouseModifiers (BTN_MIDDLE, false);
                if (auto* window = self->findWindowBySurface (surface))
                  self->handleMouseEvent (window);
            }
            ModifierKeys::currentModifiers = mods;

            if (auto* window = self->findWindowBySurface (surface))
            {
                // Clear subsurface activation state, so it can be activated again
                if (window->parentWindow)
                    window->isActivated = false;
                
                // We can't track global mouse after we leave our surface, so set it to somewhere outside the window
                self->currentMousePosition = MouseInputSource::offscreenMousePos;
                self->handleMouseEvent (window);
                
            }
            self->pointerFocused = nullptr;
        },
        .motion = [] (void* data, wl_pointer*, uint32 time, wl_fixed_t x, wl_fixed_t y)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->pointerFocused)
            {
                self->currentMouseTime = time;
                self->currentMousePosition = window->peer->localToGlobal (WaylandSymbols::getInstance()->fixedToPoint<float>(x, y));
                self->handleMouseEvent (window);
            }
        },
        .button = [] (void* data, wl_pointer*, uint32 serial, uint32 time, uint32 button, uint32 state)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->pointerFocused)
            {
                self->currentMouseSerial = serial;
                self->currentMouseTime = time;
                bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
                
                // Subsurfaces don't get a configure callback, so we need to keep track of activation manually
                if (pressed && window->parentWindow && ! window->isActivated)
                {
                    window->isActivated = true;
                    WaylandComponentPeer::isActiveApplication = true;
                    
                    self->toFront (window, true);
                    window->peer->handleBroughtToFront();
                }
                
                self->updateMouseModifiers (button, pressed);
                self->handleMouseEvent (window);
            }
        },
        .axis = [] (void* data, wl_pointer*, uint32 time, uint32 axis, wl_fixed_t value)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->pointerFocused)
            {
                self->currentMouseTime = time;
                auto scrollDistance = WaylandSymbols::getInstance()->fixedToDouble (value) / 64.0f;
                auto isVertical = axis == WL_POINTER_AXIS_VERTICAL_SCROLL;
                auto localPos = window->peer->globalToLocal (self->currentMousePosition);
                
                MouseWheelDetails wheel;
                wheel.deltaX = isVertical ? 0 : -scrollDistance;
                wheel.deltaY = isVertical ? -scrollDistance : 0;
                wheel.isReversed = false;
                wheel.isSmooth = false;
                wheel.isInertial = false;
                window->peer->handleMouseWheel (MouseInputSource::InputSourceType::mouse, localPos, time, wheel);
            }
        },
        .frame = [] (void*, wl_pointer*) {},
        .axis_source = [] (void*, wl_pointer*, uint32) {},
        .axis_stop = [] (void*, wl_pointer*, uint32, uint32) {},
        .axis_discrete = [] (void *, wl_pointer*, uint32, int32) {}
    };
    
    // Touch setup
    static const wl_touch_listener touchListener =
    {
        .down = [] (void* data, wl_touch*, uint32 serial, uint32 time, wl_surface* surface, int32 id, wl_fixed_t x, wl_fixed_t y)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->findWindowBySurface (surface))
            {
                if (window->ignoresMouse)
                    return;
                    
                self->pointerFocused = window;
                self->currentTouchTime = time;
                
                auto localPos = WaylandSymbols::getInstance()->fixedToPoint<float>(x, y);
                int touchIndex = self->currentTouches->getIndexOfTouch (window->peer, id + 1);
                
                // Update global mouse position for primary touch
                if (touchIndex == 0)
                {
                    self->currentMouseSerial = serial;
                    self->currentMousePosition = window->peer->localToGlobal (localPos);
                    self->lastFocusedWindow = (window->parentWindow || ! window->isVisible) ?
                                             self->lastFocusedWindow : window;
                }
                
                self->handleTouchEvent (window, localPos, touchIndex, MouseEventFlags::down);
            }
        },
        .up = [] (void* data, wl_touch*, uint32 serial, uint32 time, int32 id)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->pointerFocused)
            {
                int touchIndex = self->currentTouches->getIndexOfTouch (window->peer, id + 1);
                auto localPos = self->currentMousePosition;
                 
                self->currentTouchTime = time;
                 
                self->currentTouches->clearTouch (touchIndex);
                MouseEventFlags eventType = self->currentTouches->areAnyTouchesActive() ?
                                           MouseEventFlags::up : MouseEventFlags::upAndCancel;
                
                self->handleTouchEvent (window, localPos, touchIndex, eventType);
                
                // Clear touch focus if no active touches
                if (! self->currentTouches->areAnyTouchesActive())
                {
                    self->handleTouchEvent (window, MouseInputSource::offscreenMousePos, touchIndex, MouseEventFlags::up);
                }
            }
        },
        .motion = [] (void* data, wl_touch*, uint32 time, int32 id, wl_fixed_t x, wl_fixed_t y)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->pointerFocused)
            {
                int touchIndex = self->currentTouches->getIndexOfTouch (window->peer, id + 1);
                auto localPos = WaylandSymbols::getInstance()->fixedToPoint<float>(x, y);
                
                self->currentTouchTime = time;
                 
                // Update global mouse position for primary touch
                if (touchIndex == 0)
                    self->currentMousePosition = window->peer->localToGlobal (localPos);
                
                self->handleTouchEvent (window, localPos, touchIndex, MouseEventFlags::none);
            }
        },
        .frame = [] (void*, wl_touch*) {},
        .cancel = [] (void* data, wl_touch*)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (auto* window = self->pointerFocused)
            {
                auto localPos = window->peer->globalToLocal (self->currentMousePosition);
                for (auto touchIndex : self->currentTouches->getAllTouchIndicesForPeer (window->peer))
                {
                    // Send upAndCancel for each touch index
                    self->handleTouchEvent (window, localPos, touchIndex, MouseEventFlags::upAndCancel);
                }
                self->currentTouches->clear();
            }
         },
         .shape = [] (void* data, wl_touch* touch, int32 id, wl_fixed_t major, wl_fixed_t minor) {
             // TODO: this could be converted into "pressure", but I don't have a touch device that supports this, so I can't test it
         },
         .orientation = [] (void*, wl_touch*, int32, wl_fixed_t) {}
    };
       
    if (hasTouch)
    {
       currentTouches = std::make_unique<MultiTouchMapper<int32>>();
       globalTouch = WaylandSymbols::getInstance()->seatGetTouch (seat);
       WaylandSymbols::getInstance()->touchAddListener (globalTouch, &touchListener, this);
    }
    
    if (hasMouse)
    {
      globalPointer = WaylandSymbols::getInstance()->seatGetPointer (seat);
      WaylandSymbols::getInstance()->pointerAddListener (globalPointer, &pointerListener, this);
    }
    
    if (hasKeyboard)
    {
      globalKeyboard = WaylandSymbols::getInstance()->seatGetKeyboard (seat);
      WaylandSymbols::getInstance()->keyboardAddListener (globalKeyboard, &keyboardListener, this);
    }
}

void WaylandWindowSystem::setupDisplay (wl_output* output)
{
    static const wl_output_listener outputListener =
    {
        .geometry = [] (void* data, wl_output*,
                       int32 x, int32 y,
                       int32 physicalWidth, int32 physicalHeight,
                       int32,
                       const char*,
                       const char*,
                       int32)
        {
            auto* waylandDisplay = static_cast<WaylandDisplay*> (data);
            waylandDisplay->physicalWidth = physicalWidth;
            waylandDisplay->physicalHeight = physicalHeight;
            waylandDisplay->bounds = waylandDisplay->bounds.withPosition (x, y);
        },
        .mode = [] (void* data, wl_output*, uint32 flags, int32 width, int32 height, int32 refreshRate)
        {
            auto* waylandDisplay = static_cast<WaylandDisplay*> (data);
            if (flags & WL_OUTPUT_MODE_CURRENT)
            {
                waylandDisplay->bounds = waylandDisplay->bounds.withSize (width, height);
                waylandDisplay->refreshRate = refreshRate;
            }
        },
        .done = [] (void* data, wl_output* out)
        {
            auto* waylandDisplay = static_cast<WaylandDisplay*> (data);
            waylandDisplay->isDone = true;
            waylandDisplay->output = out;
        },
        .scale = [] (void* data, wl_output*, int32 factor)
        {
            auto* waylandDisplay = static_cast<WaylandDisplay*> (data);
            waylandDisplay->scaleFactor = factor;
        },
        .name = [] (void*, wl_output*, const char*) {},
        .description = [] (void*, wl_output*, const char*) {}
    };
    
    displays.emplace_back();
    WaylandSymbols::getInstance()->outputAddListener (output, &outputListener, &displays.back());
}

void WaylandWindowSystem::setupKeymap (int fd, uint32 size)
{
    if (! xkbContext || fd < 0 || size == 0 || size > 1024 * 1024)
        return;
    
    char* keymapString = static_cast<char*> (mmap (nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (keymapString == MAP_FAILED)
        return;
    
    String keymapStr;
    if (size > 0 && keymapString[size - 1] == '\0')
        keymapStr = String::fromUTF8 (keymapString, (int)size - 1);
    else
        keymapStr = String::fromUTF8 (keymapString, (int)size);
    
    if (keymap)
    {
        WaylandSymbols::getInstance()->xkbKeymapUnref (keymap);
        keymap = nullptr;
    }
    
    keymap =  WaylandSymbols::getInstance()->xkbKeymapNewFromString (xkbContext, keymapStr.toRawUTF8(),
                                                                     XKB_KEYMAP_FORMAT_TEXT_V1,
                                                                     XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    if (munmap (keymapString, size) != 0 || ! keymap)
        return;
    
    if (xkbState)
    {
        WaylandSymbols::getInstance()->xkbStateUnref (xkbState);
        xkbState = nullptr;
    }
    
    xkbState = WaylandSymbols::getInstance()->xkbStateNew (keymap);
}

void WaylandWindowSystem::setupDataDeviceCallbacks()
{
    if (! dataDeviceManager)
        return;
    
    dataDevice = WaylandSymbols::getInstance()->dataDeviceManagerGetDataDevice (dataDeviceManager, seat);
    
    static const wl_data_device_listener dataDeviceListener =
    {
        .data_offer = [] (void* data, wl_data_device*, wl_data_offer* offer)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            self->currentOffer = offer;
            self->isDraggingText = false;
            self->isDraggingFiles = false;
            
            static const wl_data_offer_listener offerListener =
            {
                .offer = [] (void*, wl_data_offer*, const char* mimeType)
                {
                    auto mime = String::fromUTF8 (mimeType);
                    if (mime == "text/uri-list")
                        getInstance()->isDraggingFiles = true;
                    else if (mime == "text/plain" || mime == "text/plain;charset=utf-8")
                        getInstance()->isDraggingText = true;
                },
                    .source_actions = [] (void*, wl_data_offer*, uint32) {},
                    .action = [] (void*, wl_data_offer*, uint32) {}
            };
            
            WaylandSymbols::getInstance()->dataOfferAddListener (offer, &offerListener, self);
        },
        .enter = [] (void* data, wl_data_device*, uint32 serial, wl_surface* surface,
                     wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            self->dragTargetWindow = self->findWindowBySurface (surface);
            self->currentOffer = offer;
            self->currentDragInfo = ComponentPeer::DragInfo{}; // Reset
            
            if (! self->dragTargetWindow || ! self->dragTargetWindow->peer)
                return;
            
            // Accept the offer first so we can read data
            if (self->isDraggingFiles)
            {
                WaylandSymbols::getInstance()->dataOfferAccept (offer, serial, "text/uri-list");
                WaylandSymbols::getInstance()->dataOfferSetActions (offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                                                    WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE,
                                                                    WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
                
                // Read file data immediately
                self->readOfferData ("text/uri-list", [self, x, y] (const String& fileList)
                {
                    self->currentDragInfo.files.clear();
                    auto lines = StringArray::fromLines (fileList);
                    for (const auto& line : lines)
                    {
                        String trimmed = line.trim();
                        if (trimmed.startsWith ("file://"))
                            self->currentDragInfo.files.add (URL::removeEscapeChars (trimmed.substring (7)));
                    }
                    self->currentDragInfo.position = WaylandSymbols::getInstance()->fixedToPoint<int> (x, y);
                    self->dragTargetWindow->peer->handleDragMove (self->currentDragInfo);
                });
            }
            else if (self->isDraggingText)
            {
                WaylandSymbols::getInstance()->dataOfferAccept (offer, serial, "text/plain");
                WaylandSymbols::getInstance()->dataOfferSetActions (offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                                                    WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
                
                // Read text data immediately
                self->readOfferData ("text/plain", [self, x, y] (const String& textData)
                {
                    self->currentDragInfo.text = textData;
                    self->currentDragInfo.position = WaylandSymbols::getInstance()->fixedToPoint<int>(x, y);
                    self->dragTargetWindow->peer->handleDragMove (self->currentDragInfo);
                });
            }
        },
        .leave = [] (void* data, wl_data_device*)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (self->dragTargetWindow && self->dragTargetWindow->peer)
                self->dragTargetWindow->peer->handleDragExit (self->currentDragInfo);
            self->dragTargetWindow = nullptr;
            self->currentOffer = nullptr;
        },
        .motion = [] (void* data, wl_data_device*, uint32, wl_fixed_t x, wl_fixed_t y)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (! self->dragTargetWindow || ! self->dragTargetWindow->peer)
                return;
            self->currentDragInfo.position = WaylandSymbols::getInstance()->fixedToPoint<int>(x, y);
            self->dragTargetWindow->peer->handleDragMove (self->currentDragInfo);
        },
        .drop = [] (void* data, wl_data_device*)
        {
            auto* self = static_cast<WaylandWindowSystem*> (data);
            if (! self->dragTargetWindow || ! self->dragTargetWindow->peer || ! self->currentOffer)
                return;
            
            if (self->dragTargetWindow->peer->handleDragDrop (self->currentDragInfo))
                WaylandSymbols::getInstance()->dataOfferFinish (self->currentOffer);
        },
        .selection = [] (void* data, wl_data_device*, wl_data_offer* offer)
        {
            auto* self = static_cast<WaylandWindowSystem*>(data);
            auto callback = [] (const String& newClipboardText)
            {
                getInstance()->currentClipboardText = newClipboardText;
            };
            
            self->currentOffer = offer;
            self->readOfferData ("UTF8_STRING", callback);
            self->readOfferData ("text/plain", callback);
            self->readOfferData ("text/plain;charset=utf-8", callback);
        }
    };
    
    WaylandSymbols::getInstance()->dataDeviceAddListener (dataDevice, &dataDeviceListener, this);
}

void WaylandWindowSystem::handleMouseEvent (WaylandWindow* window)
{
    auto localPos = window->peer->globalToLocal (currentMousePosition);
    window->peer->handleMouseEvent (MouseInputSource::InputSourceType::mouse,
                                    localPos,
                                    ModifierKeys::currentModifiers,
                                    MouseInputSource::defaultPressure,  // Default pressure
                                    MouseInputSource::defaultOrientation, // Default orientation
                                    currentMouseTime);
}

void WaylandWindowSystem::handleTouchEvent (WaylandWindow* window, Point<float> localPos, int touchIndex, MouseEventFlags eventType)
{
    ModifierKeys modsToSend = ModifierKeys::currentModifiers;
    
    if (eventType == MouseEventFlags::down)
    {
        // For touch down, simulate left button press
        ModifierKeys::currentModifiers = ModifierKeys::currentModifiers
            .withoutMouseButtons()
            .withFlags (ModifierKeys::leftButtonModifier);
        modsToSend = ModifierKeys::currentModifiers;
        
        // Send mouse-enter
        window->peer->handleMouseEvent (MouseInputSource::InputSourceType::touch,
                                     localPos,
                                     modsToSend.withoutMouseButtons(),
                                     MouseInputSource::defaultPressure,
                                     MouseInputSource::defaultOrientation,
                                     currentTouchTime, {}, touchIndex);
        
        if (! ComponentPeer::isValidPeer (window->peer)) // (in case this component was deleted by the event)
          return;
    }
    else if (eventType == MouseEventFlags::up || eventType == MouseEventFlags::upAndCancel)
    {
        modsToSend = modsToSend.withoutMouseButtons();
        
        if (eventType == MouseEventFlags::upAndCancel)
        {
            ModifierKeys::currentModifiers = ModifierKeys::currentModifiers.withoutMouseButtons();
            modsToSend = ModifierKeys::currentModifiers;
        }
    }
    
    // Send the main touch event
    window->peer->handleMouseEvent (MouseInputSource::InputSourceType::touch,
                                 localPos,
                                 modsToSend,
                                 MouseInputSource::defaultPressure,
                                 MouseInputSource::defaultOrientation,
                                 currentTouchTime, {}, touchIndex);
    
    if (! ComponentPeer::isValidPeer (window->peer)) // (in case this component was deleted by the event)
        return;
    
    // Send mouse exit event for touch up
    if (eventType == MouseEventFlags::up || eventType == MouseEventFlags::upAndCancel)
    {
        window->peer->handleMouseEvent (MouseInputSource::InputSourceType::touch,
                                     MouseInputSource::offscreenMousePos,
                                     modsToSend,
                                     MouseInputSource::defaultPressure,
                                     MouseInputSource::defaultOrientation,
                                     currentTouchTime, {}, touchIndex);
    }
}

void WaylandWindowSystem::handleKeyEvent (WaylandWindow* window, uint32 waylandKey, bool pressed)
{
    auto xkbKeyCode = static_cast<xkb_keycode_t>(waylandKey + 8);
    xkb_keysym_t keysym = WaylandSymbols::getInstance()->xkbStateKeyGetOneSym (xkbState, xkbKeyCode);
    
    auto oldMods = ModifierKeys::currentModifiers;
    ModifierKeys::currentModifiers = getNativeRealtimeModifiers();
    
    auto juceKeyCode = (int)keysym;
    bool isSpecialKey = false;
    
    // Only process special keys (function keys, navigation keys, etc.)
    if ((keysym & 0xff00) == 0xff00)
    {
        isSpecialKey = true;
        
        // First translate keypad keys to their regular equivalents
        switch (keysym)
        {
            case XKB_KEY_KP_Add:         keysym = XKB_KEY_plus;      break;
            case XKB_KEY_KP_Subtract:    keysym = XKB_KEY_minus;     break;
            case XKB_KEY_KP_Divide:      keysym = XKB_KEY_slash;     break;
            case XKB_KEY_KP_Multiply:    keysym = XKB_KEY_asterisk;  break;
            case XKB_KEY_KP_Enter:       keysym = XKB_KEY_Return;    break;
            case XKB_KEY_KP_Insert:      keysym = XKB_KEY_Insert;    break;
            case XKB_KEY_Delete:
            case XKB_KEY_KP_Delete:      keysym = XKB_KEY_Delete;    break;
            case XKB_KEY_KP_Left:        keysym = XKB_KEY_Left;      break;
            case XKB_KEY_KP_Right:       keysym = XKB_KEY_Right;     break;
            case XKB_KEY_KP_Up:          keysym = XKB_KEY_Up;        break;
            case XKB_KEY_KP_Down:        keysym = XKB_KEY_Down;      break;
            case XKB_KEY_KP_Home:        keysym = XKB_KEY_Home;      break;
            case XKB_KEY_KP_End:         keysym = XKB_KEY_End;       break;
            case XKB_KEY_KP_Page_Down:   keysym = XKB_KEY_Page_Down; break;
            case XKB_KEY_KP_Page_Up:     keysym = XKB_KEY_Page_Up;   break;
            case XKB_KEY_KP_0:           keysym = XKB_KEY_0;         break;
            case XKB_KEY_KP_1:           keysym = XKB_KEY_1;         break;
            case XKB_KEY_KP_2:           keysym = XKB_KEY_2;         break;
            case XKB_KEY_KP_3:           keysym = XKB_KEY_3;         break;
            case XKB_KEY_KP_4:           keysym = XKB_KEY_4;         break;
            case XKB_KEY_KP_5:           keysym = XKB_KEY_5;         break;
            case XKB_KEY_KP_6:           keysym = XKB_KEY_6;         break;
            case XKB_KEY_KP_7:           keysym = XKB_KEY_7;         break;
            case XKB_KEY_KP_8:           keysym = XKB_KEY_8;         break;
            case XKB_KEY_KP_9:           keysym = XKB_KEY_9;         break;
            default:
                break;
        }
        
        // Then convert to JUCE key codes
        switch (keysym)
        {
            case XKB_KEY_Return:     juceKeyCode = KeyPress::returnKey; break;
            case XKB_KEY_Escape:     juceKeyCode = KeyPress::escapeKey; break;
            case XKB_KEY_BackSpace:  juceKeyCode = KeyPress::backspaceKey; break;
            case XKB_KEY_Tab:        juceKeyCode = KeyPress::tabKey; break;
            case XKB_KEY_space:      juceKeyCode = KeyPress::spaceKey; break;
            case XKB_KEY_Delete:     juceKeyCode = KeyPress::deleteKey; break;
            case XKB_KEY_Left:       juceKeyCode = KeyPress::leftKey; break;
            case XKB_KEY_Right:      juceKeyCode = KeyPress::rightKey; break;
            case XKB_KEY_Up:         juceKeyCode = KeyPress::upKey; break;
            case XKB_KEY_Down:       juceKeyCode = KeyPress::downKey; break;
            case XKB_KEY_Page_Up:    juceKeyCode = KeyPress::pageUpKey; break;
            case XKB_KEY_Page_Down:  juceKeyCode = KeyPress::pageDownKey; break;
            case XKB_KEY_Home:       juceKeyCode = KeyPress::homeKey; break;
            case XKB_KEY_End:        juceKeyCode = KeyPress::endKey; break;
            case XKB_KEY_F1:         juceKeyCode = KeyPress::F1Key; break;
            case XKB_KEY_F2:         juceKeyCode = KeyPress::F2Key; break;
            case XKB_KEY_F3:         juceKeyCode = KeyPress::F3Key; break;
            case XKB_KEY_F4:         juceKeyCode = KeyPress::F4Key; break;
            case XKB_KEY_F5:         juceKeyCode = KeyPress::F5Key; break;
            case XKB_KEY_F6:         juceKeyCode = KeyPress::F6Key; break;
            case XKB_KEY_F7:         juceKeyCode = KeyPress::F7Key; break;
            case XKB_KEY_F8:         juceKeyCode = KeyPress::F8Key; break;
            case XKB_KEY_F9:         juceKeyCode = KeyPress::F9Key; break;
            case XKB_KEY_F10:        juceKeyCode = KeyPress::F10Key; break;
            case XKB_KEY_F11:        juceKeyCode = KeyPress::F11Key; break;
            case XKB_KEY_F12:        juceKeyCode = KeyPress::F12Key; break;
            default:
                break;
        }
    }
    
    juce_wchar textChar = 0;
    char buffer[64] = {0};
    if (pressed)
    {
        int ret = WaylandSymbols::getInstance()->xkbStateKeyGetUtf8 (xkbState, (xkb_keycode_t)xkbKeyCode, buffer, sizeof (buffer));
        if (ret > 0 && ret < (int)sizeof (buffer))
        {
            if (ret == 1)
                textChar = static_cast<juce_wchar> (buffer[0]);
        }
    }
    
    if (pressed)
    {
        lastKey = {juceKeyCode, textChar};
        keysCurrentlyDown.insert (juceKeyCode);
        keyRepeater.startTimer (keyRepeatDelay);
    }
    else
    {
        keysCurrentlyDown.erase (juceKeyCode);
        if (keysCurrentlyDown.empty())
            keyRepeater.stopTimer();
    }
    
    if (oldMods != ModifierKeys::currentModifiers)
        window->peer->handleModifierKeysChange();
    
    window->peer->handleKeyUpOrDown (pressed);
    
    if (((textChar != 0) || isSpecialKey) && pressed)
        window->peer->handleKeyPress (juceKeyCode, textChar);
}

void WaylandWindowSystem::handleKeyRepeat()
{
    if (keyboardFocused)
    {
        if (isKeyCurrentlyDown (lastKey.first))
        {
            keyboardFocused->peer->handleKeyPress (lastKey.first, lastKey.second);
            keyRepeater.startTimer (keyRepeatRate);
        }
        else
        {
            keyRepeater.stopTimer();
        }
    }
}

void WaylandWindowSystem::commit (WaylandWindow* window)
{
    if (window->currentBuffer)
        WaylandSymbols::getInstance()->surfaceAttach (window->surface, window->currentBuffer->buffer, 0, 0);
    
    WaylandSymbols::getInstance()->surfaceCommit (window->surface);
    
    // Request the next frame callback after committing
    requestFrame (window);
    
    // Commit parent surface if this is a subsurface
    if (window->parentWindow)
        WaylandSymbols::getInstance()->surfaceCommit (window->parentWindow->surface);
}

// Add this method to get display information
Array<Displays::Display> WaylandWindowSystem::findDisplays (float masterScale)
{
    // Do a roundtrip to ensure we get all output information
    WaylandSymbols::getInstance()->displayRoundtrip (display);
    
    // Wait a bit more for all displays to report "done"
    int timeout = 100; // 1 second timeout
    bool allDone = false;
    while (! allDone && timeout-- > 0)
    {
        {
            allDone = std::all_of (displays.begin(), displays.end(),
                                  [] (const auto& displayToCheck) { return displayToCheck.isDone; });
        }
        
        if (! allDone)
        {
            usleep (10000); // 10ms
            WaylandSymbols::getInstance()->displayDispatchPending (display);
        }
    }
    
    Array<Displays::Display> juceDisplays;
    if (displays.empty())
    {
        // Fallback: create a default display
        Displays::Display d;
        d.totalArea = Rectangle<int> (0, 0, 1920, 1080); // Default size
        d.userArea = d.totalArea / masterScale;
        d.isMain = true;
        d.scale = masterScale;
        d.dpi = 96.0; // Default DPI
        juceDisplays.add (d);
        return juceDisplays;
    }
    
    // Determine which output should be primary
    // In Wayland, there's no explicit "primary" output, so we'll use heuristics:
    // 1. Output at position (0,0)
    // 2. If none at (0,0), use the first one
    auto primaryIter = std::find_if (displays.begin(), displays.end(),
                                     [] (const auto& output) { return output.bounds.getPosition() == Point<int>{0, 0}; });
    if (primaryIter == displays.end() && ! displays.empty())
    {
        displays[0].isPrimary = true;
    }
    else if (primaryIter != displays.end())
    {
        (*primaryIter).isPrimary = true;
    }
    
    // Convert Wayland displays to JUCE Display objects
    for (const auto& waylandDisplay : displays)
    {
        if (! waylandDisplay.isDone || waylandDisplay.bounds.isEmpty())
            continue;
        
        Displays::Display d;
        d.totalArea = waylandDisplay.bounds;
        d.isMain = waylandDisplay.isPrimary;
        
        // Calculate DPI from physical dimensions
        if (waylandDisplay.physicalWidth > 0 && waylandDisplay.physicalHeight > 0)
        {
            // Convert mm to inches and calculate DPI
            auto widthInches = waylandDisplay.physicalWidth / 25.4;
            auto heightInches = waylandDisplay.physicalHeight / 25.4;
            auto dpiX = waylandDisplay.bounds.getWidth() / widthInches;
            auto dpiY = waylandDisplay.bounds.getHeight() / heightInches;
            d.dpi = (dpiX + dpiY) * 0.5; // Average of x and y DPI
        }
        else
        {
            d.dpi = 96.0; // Fallback DPI
        }
        
        // Apply Wayland's scale factor and master scale
        d.scale = masterScale * waylandDisplay.scaleFactor;
        d.userArea = d.totalArea / d.scale;
        
        // Set refresh rate if available
        if (waylandDisplay.refreshRate > 0)
            d.verticalFrequencyHz = waylandDisplay.refreshRate / 1000.0; // Convert mHz to Hz
        
        // Add primary display first
        if (d.isMain)
            juceDisplays.insert (0, d);
        else
            juceDisplays.add (d);
    }
    
    if (! juceDisplays.isEmpty() && ! juceDisplays.getReference (0).isMain)
    {
        juceDisplays.getReference (0).isMain = true;
    }
    
    return juceDisplays;
}

void WaylandWindowSystem::updateCursor()
{
    if (! cursorSurface)
    {
        cursorTheme = WaylandSymbols::getInstance()->cursorThemeLoad (nullptr, 24, shm);
        if (cursorTheme == nullptr)
            return;

        loadStandardCursors();
        cursorSurface = WaylandSymbols::getInstance()->compositorCreateSurface (compositor);
        if (cursorSurface == nullptr)
            return;

        currentCursor = createStandardMouseCursor (MouseCursor::NormalCursor);
    }

    if (currentCursor.standardCursor == -1)
    {
        auto& image = currentCursor.cursorImage;
        if (image != nullptr && image->buffer != nullptr)
        {
            WaylandSymbols::getInstance()->surfaceAttach (cursorSurface, image->buffer, 0, 0);
            WaylandSymbols::getInstance()->surfaceDamage (cursorSurface, 0, 0, image->width, image->height);
            WaylandSymbols::getInstance()->surfaceCommit (cursorSurface);
            WaylandSymbols::getInstance()->pointerSetCursor (globalPointer, currentMouseSerial, cursorSurface,
                                    image->hotspotX, image->hotspotY);
        }
    }
    else if (currentCursor.standardCursor == MouseCursor::NoCursor)
    {
        WaylandSymbols::getInstance()->pointerSetCursor (globalPointer, currentMouseSerial, nullptr, 0, 0);
    }
    else if (currentCursor.standardCursor >= 0 && currentCursor.standardCursor < 20)
    {
        auto* cursor = standardCursors[currentCursor.standardCursor];

        // The theme didn't have this cursor or any of its fallbacks. Try
        // NormalCursor as a last resort before giving up.
        if (cursor == nullptr || cursor->image_count == 0)
            cursor = standardCursors[MouseCursor::NormalCursor];

        if (cursor == nullptr || cursor->image_count == 0)
            return;

        auto* cursorImage = cursor->images[0];
        if (cursorImage == nullptr)
            return;

        auto* cursorBuffer = WaylandSymbols::getInstance()->cursorImageGetBuffer (cursorImage);
        if (cursorBuffer == nullptr)
            return;

        WaylandSymbols::getInstance()->surfaceAttach (cursorSurface, cursorBuffer, 0, 0);
        WaylandSymbols::getInstance()->surfaceDamage (cursorSurface, 0, 0,
                             (int) cursorImage->width, (int) cursorImage->height);
        WaylandSymbols::getInstance()->surfaceCommit (cursorSurface);
        WaylandSymbols::getInstance()->pointerSetCursor (globalPointer, currentMouseSerial, cursorSurface,
                                (int) cursorImage->hotspot_x, (int) cursorImage->hotspot_y);
    }
}

void WaylandWindowSystem::loadStandardCursors()
{
    if (! cursorTheme)
        return;

    static const std::pair<int, std::initializer_list<const char*>> mappings[] = {
        {MouseCursor::ParentCursor,                  {"default", "left_ptr", "arrow"}},
        {MouseCursor::NormalCursor,                  {"default", "left_ptr", "arrow"}},
        {MouseCursor::WaitCursor,                    {"wait", "watch"}},
        {MouseCursor::IBeamCursor,                   {"text", "xterm"}},
        {MouseCursor::CrosshairCursor,               {"crosshair", "cross"}},
        {MouseCursor::CopyingCursor,                 {"copy", "dnd-copy"}},
        {MouseCursor::PointingHandCursor,            {"pointer", "hand2", "hand1"}},
        {MouseCursor::DraggingHandCursor,            {"grabbing", "closedhand", "grab", "openhand"}},
        {MouseCursor::LeftRightResizeCursor,         {"ew-resize", "col-resize", "sb_h_double_arrow", "h_double_arrow"}},
        {MouseCursor::UpDownResizeCursor,            {"ns-resize", "row-resize", "sb_v_double_arrow", "v_double_arrow"}},
        {MouseCursor::UpDownLeftRightResizeCursor,   {"all-scroll", "fleur", "size_all", "move"}},
        {MouseCursor::TopEdgeResizeCursor,           {"n-resize", "top_side"}},
        {MouseCursor::BottomEdgeResizeCursor,        {"s-resize", "bottom_side"}},
        {MouseCursor::LeftEdgeResizeCursor,          {"w-resize", "left_side"}},
        {MouseCursor::RightEdgeResizeCursor,         {"e-resize", "right_side"}},
        {MouseCursor::TopLeftCornerResizeCursor,     {"nw-resize", "top_left_corner"}},
        {MouseCursor::TopRightCornerResizeCursor,    {"ne-resize", "top_right_corner"}},
        {MouseCursor::BottomLeftCornerResizeCursor,  {"sw-resize", "bottom_left_corner"}},
        {MouseCursor::BottomRightCornerResizeCursor, {"se-resize", "bottom_right_corner"}}
    };

    wl_cursor* universal = nullptr;
    for (const char* name : { "default", "left_ptr", "arrow" })
    {
        universal = WaylandSymbols::getInstance()->cursorThemeGetCursor (cursorTheme, name);
        if (universal != nullptr)
            break;
    }

    for (const auto& mapping : mappings)
    {
        wl_cursor* found = nullptr;
        for (const char* name : mapping.second)
        {
            found = WaylandSymbols::getInstance()->cursorThemeGetCursor (cursorTheme, name);
            if (found != nullptr)
                break;
        }
        standardCursors[mapping.first] = (found != nullptr) ? found : universal;
    }
}

WaylandCursor WaylandWindowSystem::createCustomMouseCursorInfo (const Image& image, Point<int> hotspot)
{
    WaylandCursor cursor;
    cursor.standardCursor = -1;
    
    auto imageW = image.getWidth();
    auto imageH = image.getHeight();
    
    auto argbImage = image.convertedToFormat (Image::ARGB);
    
    cursor.cursorImage = new WaylandShmBuffer (imageW, imageH);
    
    // Copy image data to buffer
    uint32* bufferData = static_cast<uint32*> (cursor.cursorImage->data);
    const Image::BitmapData srcData (argbImage, Image::BitmapData::readOnly);
    
    for (int y = 0; y < (int)imageH; ++y)
    {
        auto* srcPixel = reinterpret_cast<uint32*> (srcData.getLinePointer (y));
        auto* destRow = bufferData + (y * imageW);
        for (int x = 0; x < (int)imageW; ++x)
            destRow[x] = srcPixel[x];
    }
    
    cursor.cursorImage->hotspotX = hotspot.x;
    cursor.cursorImage->hotspotY = hotspot.y;
    
    return cursor;
}

void WaylandWindowSystem::deleteMouseCursor (WaylandCursor cursorHandle)
{
    if (cursorHandle.standardCursor == -1)
        delete cursorHandle.cursorImage;
}

WaylandCursor WaylandWindowSystem::createStandardMouseCursor (MouseCursor::StandardCursorType type)
{
    WaylandCursor cursor;
    cursor.standardCursor = (int)type;
    cursor.cursorImage = nullptr;
    return cursor;
}

void WaylandWindowSystem::showCursor (WaylandCursor cursorHandle)
{
    currentCursor = cursorHandle;
    updateCursor();
}

void WaylandWindowSystem::readOfferData (const String& mimeType, std::function<void (const String&)> callback) const
{
    int pipeFds[2];
    if (! currentOffer || pipe (pipeFds) == -1)
        return;
    
    WaylandSymbols::getInstance()->dataOfferReceive (currentOffer, mimeType.toRawUTF8(), pipeFds[1]);
    close (pipeFds[1]);
    
    WaylandSymbols::getInstance()->displayRoundtrip (display);
    
    // Wait for data to become available
    fd_set fds;
    FD_ZERO (&fds);
    FD_SET (pipeFds[0], &fds);
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    int ret = select (pipeFds[0] + 1, &fds, nullptr, nullptr, &timeout);
    String data;
    if (ret > 0 && FD_ISSET (pipeFds[0], &fds))
    {
        char buffer[4096];
        ssize_t bytesRead;
        
        // Set non-blocking
        int flags = fcntl (pipeFds[0], F_GETFL, 0);
        fcntl (pipeFds[0], F_SETFL, flags | O_NONBLOCK);
        
        while ((bytesRead = read (pipeFds[0], buffer, sizeof (buffer))) > 0)
            data += String::createStringFromData (buffer, static_cast<int>(bytesRead));
    }
    
    close (pipeFds[0]);
    callback (data);
}

void WaylandWindowSystem::copyTextToClipboard (const String& text)
{
    if (! dataDeviceManager || text.isEmpty())
        return;
    
    currentClipboardText = text;
    
    clipboardSource = WaylandSymbols::getInstance()->dataDeviceManagerCreateDataSource (dataDeviceManager);
    
    WaylandSymbols::getInstance()->dataSourceOffer (clipboardSource, "text/plain");
    WaylandSymbols::getInstance()->dataSourceOffer (clipboardSource, "text/plain;charset=utf-8");
    WaylandSymbols::getInstance()->dataSourceOffer (clipboardSource, "UTF8_STRING");
    
    static const wl_data_source_listener clipboardSourceListener = {
            .target = [] (void*, wl_data_source*, const char*) {},
            .send = [] (void* data, wl_data_source* source, const char*, int32 fd)
            {
                auto* self = static_cast<WaylandWindowSystem*> (data);
                if (source == self->clipboardSource)
                {
                    self->writeDataToFd (fd, self->currentClipboardText);
                }
            },
            .cancelled = [] (void*, wl_data_source*) {},
            .dnd_drop_performed = [] (void*, wl_data_source*) {},
            .dnd_finished = [] (void*, wl_data_source*) {},
            .action = [] (void*, wl_data_source*, uint32) {}
    };
    
    WaylandSymbols::getInstance()->dataSourceAddListener (clipboardSource, &clipboardSourceListener, this);
    WaylandSymbols::getInstance()->dataDeviceSetSelection (dataDevice, clipboardSource, currentMouseSerial);
}

String WaylandWindowSystem::getTextFromClipboard() const
{
    return currentClipboardText;
}

bool WaylandWindowSystem::externalDragFileInit (WaylandWindow* window, const StringArray& files, bool canMoveFiles, std::function<void()>&& callback)
{
    if (files.isEmpty())
        return false;
    
    dragFiles = files;
    dragCompletionCallback = std::move (callback);
    dragSource = WaylandSymbols::getInstance()->dataDeviceManagerCreateDataSource (dataDeviceManager);
    setupDragSource (window, false, canMoveFiles);
    return true;
}

bool WaylandWindowSystem::externalDragTextInit (WaylandWindow* window, const String& text, std::function<void()>&& callback)
{
    if (text.isEmpty())
        return false;
    
    dragText = text;
    dragCompletionCallback = std::move (callback);
    dragSource = WaylandSymbols::getInstance()->dataDeviceManagerCreateDataSource (dataDeviceManager);
    setupDragSource (window, true, false);
    return true;
}

void WaylandWindowSystem::setupDragSource (WaylandWindow* window, bool isText, bool canMoveFiles)
{
    static const wl_data_source_listener dragSourceListener = {
            .target = [] (void*, wl_data_source*, const char*) {},
            .send = [] (void* data, wl_data_source* source, const char* mimeType, int32 fd)
            {
                auto* self = static_cast<WaylandWindowSystem*> (data);
                if (source == self->dragSource) {
                    auto mime = String::fromUTF8 (mimeType);
                    if (mime == "text/uri-list" && ! self->dragFiles.isEmpty())
                    {
                        // Convert file paths to URI list
                        StringArray uris;
                        for (const auto& file : self->dragFiles)
                            uris.add ("file://" + file);

                        self->writeDataToFd (fd, uris.joinIntoString ("\r\n") + "\r\n");
                    }
                    else if (mime.startsWith ("text/"))
                    {
                        self->writeDataToFd (fd, self->dragText);
                    }
                }
            },
            .cancelled = [] (void* data, wl_data_source* source)
            {
                auto* self = static_cast<WaylandWindowSystem*> (data);
                if (source == self->dragSource)
                {
                    if (self->dragCompletionCallback)
                        self->dragCompletionCallback();
                    self->dragCompletionCallback = nullptr;
                }
            },
            .dnd_drop_performed = [] (void* data, wl_data_source* source)
            {
                auto* self = static_cast<WaylandWindowSystem*> (data);
                if (source == self->dragSource)
                {
                    if (self->dragCompletionCallback)
                        self->dragCompletionCallback();
                    self->dragCompletionCallback = nullptr;
                }
            },
            .dnd_finished = [] (void* data, wl_data_source* source)
            {
                auto* self = static_cast<WaylandWindowSystem*> (data);
                if (source == self->dragSource)
                {
                    if (self->dragCompletionCallback)
                        self->dragCompletionCallback();
                    self->dragCompletionCallback = nullptr;
                }
            },
            .action = [] (void*, wl_data_source*, uint32) {}
    };
    
    
    if (isText)
    {
        WaylandSymbols::getInstance()->dataSourceOffer (dragSource, "text/plain");
        WaylandSymbols::getInstance()->dataSourceOffer (dragSource, "text/plain;charset=utf-8");
        WaylandSymbols::getInstance()->dataSourceOffer (dragSource, "UTF8_STRING");
    }
    else
    {
        WaylandSymbols::getInstance()->dataSourceOffer (dragSource, "text/uri-list");
        WaylandSymbols::getInstance()->dataSourceOffer (dragSource, "text/plain");
        
        // Set supported actions
        if (canMoveFiles)
            WaylandSymbols::getInstance()->dataSourceSetActions (dragSource, WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        else
            WaylandSymbols::getInstance()->dataSourceSetActions (dragSource, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    }
    
    WaylandSymbols::getInstance()->dataSourceAddListener (dragSource, &dragSourceListener, this);
    WaylandSymbols::getInstance()->dataDeviceStartDrag (dataDevice, dragSource, window->surface, nullptr, currentMouseSerial);
}

void WaylandWindowSystem::writeDataToFd (int fd, const String& data) const
{
    auto utf8 = data.toUTF8();
    int64 written = 0;
    int64 total = (int64)utf8.sizeInBytes() - 1; // Exclude null terminator
    
    while (written < static_cast<ssize_t> (total))
    {
        ssize_t result = write (fd, utf8.getAddress() + written, (uint64)(total - written));
        if (result < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            break;
        }
        written += result;
    }
    
    close (fd);
}

bool WaylandWindowSystem::isKeyCurrentlyDown (int keyCode)
{
    return keysCurrentlyDown.find (keyCode) != keysCurrentlyDown.cend();
}

namespace WaylandMessageLoop
{
void prepareWaylandFd()
{
    auto* displayManager = WaylandWindowSystem::getInstance();
    if (! displayManager->isWaylandAvailable())
        return;
    
    auto display = displayManager->getDisplay();
    while (WaylandSymbols::getInstance()->displayPrepareRead (display) != 0)
        WaylandSymbols::getInstance()->displayDispatchPending (display);
    
    WaylandSymbols::getInstance()->displayFlush (display);
}

void processWaylandFd()
{
    auto* displayManager = WaylandWindowSystem::getInstance();
    if (! displayManager->isWaylandAvailable())
        return;
    
    auto display = displayManager->getDisplay();
    WaylandSymbols::getInstance()->displayReadEvents (display);
    WaylandSymbols::getInstance()->displayDispatchPending (display);
}
}

} // namespace juce
