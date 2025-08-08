/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 7 End-User License
   Agreement and JUCE Privacy Policy.

   End User License Agreement: www.juce.com/juce-7-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/



using namespace wayland;

namespace juce
{

struct WaylandShmBuffer {
  wayland::buffer_t buffer;
  void* data = nullptr;
  size_t size = 0;
  int fd = -1;
  int width = 0;
  int height = 0;
  wayland::shm_pool_t pool;

  int hotspotX = 0;
  int hotspotY = 0;
  
  WaylandShmBuffer(int buffer_width, int buffer_height)
  {
        width = buffer_width;
        height = buffer_height;

        const int stride = buffer_width * 4; // 4 bytes per pixel (ARGB8888)
        size = stride * buffer_height;

        fd = [](off_t bufsize){
        #if JUCE_LINUX
            // Try memfd_create (Linux-specific)
            int memfd = syscall(SYS_memfd_create, "wayland-shm", MFD_CLOEXEC);
            if (memfd >= 0) {
                if (ftruncate(memfd, bufsize) == 0) {
                    return memfd;
                }
                close(memfd);
            }
        #endif

            char name[] = "/tmp/wayland-shm-XXXXXX";
            int tempfd = mkstemp(name);
            if (tempfd < 0) {
                return -1;
            }
            unlink(name);  // Remove file from filesystem but keep fd open
            
            if (ftruncate(tempfd, bufsize) < 0) {
                close(tempfd);
                return -1;
            }
            return tempfd;
            }(size);
        
        if (fd < 0) {
            throw std::runtime_error("Failed to create shared memory file");
        }

        // mmap the shared memory
        data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            close(fd);
            fd = -1;
            throw std::runtime_error("Failed to mmap shared memory");
        }
        
        // Create the wl_shm_pool using shared shm
        pool = WaylandWindowSystem::getInstance()->getShm().create_pool(fd, size);
        

        // Create wl_buffer from pool
        buffer = pool.create_buffer(
            0,          // offset
            buffer_width,
            buffer_height,
            stride,
            wayland::shm_format::argb8888
        );
  }
  
  ~WaylandShmBuffer() {
      if (data && data != MAP_FAILED) {
          munmap(data, size);
      }
      if (fd >= 0) {
          close(fd);
      }
  }
};


namespace WaylandMessageLoop
{
    void prepareWaylandFd()
    {
        auto* displayManager = WaylandWindowSystem::getInstance();
        if(!displayManager->isWaylandAvailable()) return;
        auto& display = displayManager->getDisplay();
        
        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }
        
        wl_display_flush(display);
    }
    
    void processWaylandFd()
    {
        auto* displayManager = WaylandWindowSystem::getInstance();
        if(!displayManager->isWaylandAvailable()) return;
        auto& display = displayManager->getDisplay();
        
        wl_display_read_events(display); 
        wl_display_dispatch_pending(display); 
    }
}

struct WaylandWindow {
    wayland::surface_t surface;
    wayland::subsurface_t subsurface;
    libdecor_frame* frame = nullptr;
    wayland::callback_t frameCallback;
    
    std::unique_ptr<WaylandShmBuffer> currentBuffer = nullptr;
    Rectangle<int> bounds;
    
    bool visible:1 = true;
    bool configured:1 = false;
    bool fullscreen:1 = false;
    bool minimised:1 = false;
    bool ignoresMouse:1 = false;
    bool ignoresKeyboard:1 = false;
    WaylandWindow* parentWindow = nullptr;
    
    Point<float> currentMousePosition;
    WaylandComponentPeer* peer;
};

struct WaylandOutput {
    wayland::output_t output;
    int32_t x = 0, y = 0;
    int32_t width = 0, height = 0;
    int32_t physical_width = 0, physical_height = 0;
    int32_t refresh_rate = 0;
    int32_t scale_factor = 1;
    wayland::output_transform transform = wayland::output_transform::normal;
    std::string make, model, name;
    bool done = false;
    bool is_primary = false;
    
    WaylandOutput(wayland::output_t out) : output(std::move(out)) {
        output.on_geometry() = [this](int32_t x_pos, int32_t y_pos, 
                                     int32_t phys_width, int32_t phys_height,
                                     wayland::output_subpixel subpixel,
                                     std::string const& make_str,
                                     std::string const& model_str,
                                     wayland::output_transform trans) {
            x = x_pos;
            y = y_pos;
            physical_width = phys_width;
            physical_height = phys_height;
            make = make_str;
            model = model_str;
            transform = trans;
        };
        
        output.on_mode() = [this](wayland::output_mode flags, int32_t w, int32_t h, int32_t refresh) {
            if (flags & wayland::output_mode::current) {
                width = w;
                height = h;
                refresh_rate = refresh;
            }
        };
        
        output.on_scale() = [this](int32_t factor) {
            scale_factor = factor;
        };
        
        output.on_name() = [this](std::string const& output_name) {
            name = output_name;
        };
        
        output.on_done() = [this]() {
            done = true;
        };
    }
};

struct libdecor_interface WaylandWindowSystem::decorInterface = {
    .error = [](struct libdecor* context, enum libdecor_error error, const char* message) {
        std::cerr << "Libdecor error " << error << ": " << message << std::endl;
    }
};

// Frame interface callbacks
static void frame_configure(struct libdecor_frame* frame,
                           struct libdecor_configuration* configuration,
                           void* user_data) {
    auto* window = static_cast<WaylandWindow*>(user_data);
    
    // Handle window states
    enum libdecor_window_state window_state;
    if (libdecor_configuration_get_window_state(configuration, &window_state)) {
        window->fullscreen = window_state & LIBDECOR_WINDOW_STATE_MAXIMIZED;
        window->minimised = false; // libdecor doesn't expose minimized state
    }
    
    window->configured = true;
    
    int width, height;
    if (libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
        if (width > 0 && height > 0) {
            window->bounds.setSize(width, height);
            window->peer->setBounds(Rectangle<int>(0, 0, width, height), window->fullscreen);
            window->peer->repaint(Rectangle<int>(0, 0, width, height));
        }
    }

    // Create and commit the configuration
    auto* state = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, state, configuration);
    libdecor_state_free(state);
    
        
    if(!window->ignoresMouse) {
        auto inputRegion = WaylandWindowSystem::getInstance()->getCompositor().create_region();
        inputRegion.add(0, 0, width, height);
        window->surface.set_input_region(inputRegion);
    }
}

static void frame_close(struct libdecor_frame* frame, void* user_data) {
    auto* window = static_cast<WaylandWindow*>(user_data);
    window->visible = false;
}

static void frame_commit(struct libdecor_frame* frame, void* user_data) {
    // Optional: handle commit events if needed
}

static struct libdecor_frame_interface frame_interface = {
    .configure = frame_configure,
    .close = frame_close,
    .commit = frame_commit,
};

WaylandWindowSystem::WaylandWindowSystem()
{
    if (initialised) return;
    
    // Check if we're actually running under Wayland
    if (!std::getenv("WAYLAND_DISPLAY")) {
        return;
    }

    try {
        // initialise XKB context for this window
        xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!xkbContext) {
            std::cerr << "Failed to create XKB context for window" << std::endl;
        }

        display = std::make_unique<wayland::display_t>();
        if (!*display) {
            return;
        }
        
        registry = display->get_registry();
        if (!registry) {
            return;
        }
        
        registry.on_global() = [this](uint32_t name, std::string const& interface, uint32_t version) mutable {
            if (interface == "wl_compositor") {
                registry.bind(name, compositor, std::min(version, 4u));
            }
            if (interface == "wl_subcompositor") {
                registry.bind(name, subcompositor, version);
            }
            else if (interface == "wl_shm") {
                registry.bind(name, shm, std::min(version, 1u));
            }
            else if (interface == "wl_seat") {
                registry.bind(name, seat, std::min(version, 7u));
                setupGlobalInput();
            }
            else if (interface == "wl_data_device_manager") 
            {
                 registry.bind(name, dataDeviceManager, version);
            }
            else if (interface == "wl_output") {
                wayland::output_t output_proxy;
                registry.bind(name, output_proxy, std::min(version, 4u));
                auto wayland_output = std::make_unique<WaylandOutput>(std::move(output_proxy));
                outputs.push_back(std::move(wayland_output));
            }
        };

        display->roundtrip();
        
        // Check for required globals
        std::string missing_globals;
        if (!compositor) missing_globals += "wl_compositor ";
        if (!subcompositor) missing_globals += "wl_subcompositor ";        
        if (!shm) missing_globals += "wl_shm ";
        if (!seat) missing_globals += "wl_seat ";
        if (!dataDeviceManager) missing_globals += "wl_data_device_manager ";
        if (!missing_globals.empty()) {
            throw std::runtime_error("Missing required Wayland globals: " + missing_globals);
        }
        
        decorator = libdecor_new(*display, &decorInterface);
        if (!decorator) {
            throw std::runtime_error("Failed to create libdecor context");
        }
        
        initializeDataDeviceManager();

        // Register the event loop callback
        LinuxEventLoop::registerFdCallback(display->get_fd(), [](int fd) mutable {});
        
        initialised = true;        
    } catch (...) {
        std::cerr << "Wayland not found" << std::endl;
        return;
    }
}

WaylandWindowSystem::~WaylandWindowSystem()
{
    customCursors.clear();
    if (cursorTheme) {
        cursorTheme = cursor_theme_t{};
    }
    
    if (decorator) {
        libdecor_unref(decorator);
        decorator = nullptr;
    }
    
    clearSingletonInstance();
}

WaylandWindow* WaylandWindowSystem::createWindow(bool isSubsurface, ComponentPeer* peer, WaylandWindow* parent)
{
    auto* window = new WaylandWindow();
    window->peer = dynamic_cast<WaylandComponentPeer*>(peer);

    if(isSubsurface && !parent)
    {
        window->parentWindow = lastFocusedWindow;
        if(!window->parentWindow)
            isSubsurface = false; // fall back to regular window if we have no parent to attach to
    }
    else {
        window->parentWindow = parent;
    }
    
    window->surface = getCompositor().create_surface();
    window->surface.set_buffer_scale(peer->getPlatformScaleFactor());
    window->ignoresMouse = (peer->getStyleFlags() & ComponentPeer::windowIgnoresMouseClicks);
    if(window->ignoresMouse)
    {
        window->surface.set_input_region(compositor.create_region());
    }

    if(isSubsurface)
    {
        window->subsurface = subcompositor.get_subsurface(window->surface, window->parentWindow->surface);
        window->subsurface.set_position(0, 0);
        window->subsurface.set_desync(); 

        window->surface.commit();
        window->parentWindow->surface.commit();
    }
    else 
    {  
       auto styleFlags = peer->getStyleFlags();
        
        int capabilities = LIBDECOR_ACTION_MOVE;        
        if (styleFlags & ComponentPeer::windowHasMinimiseButton) {
            capabilities |= (int)LIBDECOR_ACTION_MINIMIZE;
        }
        
        if (styleFlags & ComponentPeer::windowHasMaximiseButton) {
            capabilities |= (int)LIBDECOR_ACTION_FULLSCREEN;
        }
        
        if (styleFlags & ComponentPeer::windowHasCloseButton) {
            capabilities |= (int)LIBDECOR_ACTION_CLOSE;
        }
        
        if (styleFlags & ComponentPeer::windowIsResizable) {
           capabilities |= (int)LIBDECOR_ACTION_RESIZE;
        }
        window->frame = libdecor_decorate(decorator, window->surface, 
                                              &frame_interface, window);
        libdecor_frame_set_capabilities(window->frame, (libdecor_capabilities)capabilities);
         if (!(styleFlags & ComponentPeer::windowHasTitleBar)) {
            libdecor_frame_set_visibility(window->frame, false);
        }
        
        
        if (!window->frame) {
            delete window;
            throw std::runtime_error("Failed to create libdecor frame");
        }
        
        // Set initial title
        libdecor_frame_set_title(window->frame, "JUCE Window");
        
        libdecor_frame_set_min_content_size(window->frame, 500, 500);
        libdecor_frame_set_max_content_size(window->frame, 5000, 5000);
				    
        // Map the surface
        libdecor_frame_map(window->frame);
        
        window->surface.commit();
        // Wait for initial configure
        int timeout = 50;
        while (!window->configured && timeout-- > 0) {
            display->dispatch_pending();
            if (!window->configured) {
                usleep(10000);
                getDisplay().dispatch();
            }
        }
    }
    

    // Make sure dialogs can properly attach to a parent
    lastFocusedWindow = window->subsurface ? lastFocusedWindow : window;     
    
    try
    {
        surfaceToWindow[window->surface.get_id()] = window;
    }
    catch(...)
    {
        std::cout << "Failed to register window" << std::endl;
    }
    
    zOrder.push_back(window);
    
    return window;
}

WaylandWindow* WaylandWindowSystem::getWaylandWindowForPeer(ComponentPeer* peer)
{
    if(auto* waylandPeer = dynamic_cast<WaylandComponentPeer*>(peer))
    {
        return waylandPeer->getWindow();
    }

    return nullptr;
};


void WaylandWindowSystem::requestFrame(WaylandWindow* window)
{
    window->frameCallback = window->surface.frame();
    window->frameCallback.on_done() = [window](uint32_t next_time) {
        window->peer->onFrame();
    };
    window->surface.commit();
}

void WaylandWindowSystem::destroyWindow(WaylandWindow* window)
{
  auto it = findInZOrder(window);
  if (it != zOrder.end()) {
      zOrder.erase(it);
  }
  
  if (window->frame) {
      libdecor_frame_unref(window->frame);
      window->frame = nullptr;
  }

  try
  {
      auto it = surfaceToWindow.find(window->surface.get_id());
      if (it != surfaceToWindow.end()) {
            surfaceToWindow.erase(it);
            // Clear focus if this window had it
            if (keyboardFocused == window) keyboardFocused = nullptr;
            if (pointerFocused == window) pointerFocused = nullptr;
            if(lastFocusedWindow == window) lastFocusedWindow = nullptr;
        }
    }
    catch(...)
    {
        std::cout << "Failed to unregister window" << std::endl;
    }
    delete window;
}

void WaylandWindowSystem::setBounds(WaylandWindow* window, Rectangle<int> b)
{
    if(b == window->bounds) 
        return;

    if (window->frame) {
        window->bounds = b.withZeroOrigin();        
        auto* state = libdecor_state_new(b.getWidth(), b.getHeight());
        libdecor_frame_commit(window->frame, state, nullptr);
    } else if (window->subsurface) {
        auto parentBounds = getBounds(window->parentWindow);
        window->bounds = b.translated(parentBounds.getX(), parentBounds.getY());
        window->subsurface.set_position(window->bounds.getX(), window->bounds.getY()); 
        window->surface.commit();
        window->parentWindow->surface.commit();
    }
}


Rectangle<int> WaylandWindowSystem::getBounds(WaylandWindow* window)
{
    auto bounds = window->bounds;
    return bounds;
}
void WaylandWindowSystem::startHostManagedResize (WaylandWindow* window, ResizableBorderComponent::Zone zone)
{    
    if (!window->frame) return; // Only for decorated windows
    
    auto edge = [zone] {
        using F = ResizableBorderComponent::Zone::Zones;
        switch (zone.getZoneFlags()) {
            case F::top | F::left:      return LIBDECOR_RESIZE_EDGE_TOP_LEFT;
            case F::top:                return LIBDECOR_RESIZE_EDGE_TOP;
            case F::top | F::right:     return LIBDECOR_RESIZE_EDGE_TOP_RIGHT;
            case F::right:              return LIBDECOR_RESIZE_EDGE_RIGHT;
            case F::bottom | F::right:  return LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT;
            case F::bottom:             return LIBDECOR_RESIZE_EDGE_BOTTOM;
            case F::bottom | F::left:   return LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT;
            case F::left:               return LIBDECOR_RESIZE_EDGE_LEFT;
        }
        return LIBDECOR_RESIZE_EDGE_NONE;
    }();
    
    if (edge == LIBDECOR_RESIZE_EDGE_NONE) {
        libdecor_frame_move(window->frame, seat, currentMouseSerial);
    } else {
        libdecor_frame_resize(window->frame, seat, currentMouseSerial, edge);
    }
}

bool isParentOf(WaylandWindow* window, WaylandWindow* parentWindow)
{
    if(window->parentWindow == nullptr)
    {
        return false;
    }
    if(window->parentWindow == parentWindow)
    {
        return true;
    }
    
    return isParentOf(window->parentWindow, parentWindow);
}

    // Helper to get all subsurfaces with the same parent, sorted by z-order
std::vector<WaylandWindow*> WaylandWindowSystem::getSubsurfacesForParent(WaylandWindow* parent) const {
    std::vector<WaylandWindow*> subsurfaces;
    for (WaylandWindow* window : zOrder) {
        if ((window->subsurface && window->parentWindow == parent) || window == parent) {
            subsurfaces.push_back(window);
        }
    }
    return subsurfaces;
}


void WaylandWindowSystem::toFront(WaylandWindow* window) {
    if (!window->subsurface) {
        return;
    }

    auto it = findInZOrder(window);
    if (it == zOrder.end()) return;

    zOrder.erase(it);
    zOrder.push_back(window);
    enforceOrder();
}

void WaylandWindowSystem::toBehind(WaylandWindow* window, WaylandWindow* referenceWindow) {
    if (!window->subsurface || window->parentWindow != referenceWindow->parentWindow) {
        return; // Can only reorder subsurfaces with same parent
    }
    
    auto windowIt = findInZOrder(window);
    auto refIt = findInZOrder(referenceWindow);
    
    if (windowIt == zOrder.end() || refIt == zOrder.end()) return;

    // Store indices before any modifications
    size_t windowIndex = std::distance(zOrder.begin(), windowIt);
    size_t refIndex = std::distance(zOrder.begin(), refIt);
    
    // Remove window from current position
    zOrder.erase(zOrder.begin() + windowIndex);
    
    // Adjust reference index if it was after the removed window
    if (refIndex > windowIndex) {
        refIndex--;
    }
    
    // Insert before the reference window
    zOrder.insert(zOrder.begin() + refIndex, window);
    window->subsurface.place_below(referenceWindow->surface);
    window->surface.commit();
    referenceWindow->surface.commit();
}

void WaylandWindowSystem::enforceOrder() {
    // Group subsurfaces by parent and enforce order for each group
    std::set<WaylandWindow*> processedParents;
    
    for (WaylandWindow* window : zOrder) {
        if (window->parentWindow && 
            processedParents.find(window->parentWindow) == processedParents.end()) {
            auto* parent = window->parentWindow;
            // Get all subsurfaces for this parent in z-order
            std::vector<WaylandWindow*> subsurfaces = getSubsurfacesForParent(parent);

            if (subsurfaces.size() <= 1) return; // Nothing to stack

            // Work from bottom to top, making sure each window is placed above the previous one
            for (size_t i = 1; i < subsurfaces.size(); i++) {
                WaylandWindow* current = subsurfaces[i];
                WaylandWindow* below = subsurfaces[i-1];
                current->subsurface.place_above(below->surface);
            }

            // Commit all changes
            for (WaylandWindow* subsurface : subsurfaces) {
                subsurface->surface.commit();
            }
            parent->surface.commit();

            processedParents.insert(window->parentWindow);
        }
    }
}

void WaylandWindowSystem::setVisible (WaylandWindow* window, bool shouldBeVisible)
{
    window->visible = shouldBeVisible;
    if(shouldBeVisible)
    {
        if(window->currentBuffer) {
            window->surface.attach(window->currentBuffer->buffer, 0, 0);
            window->surface.commit();
        }
        requestFrame(window);
        if(window->subsurface)
        {
            window->parentWindow->surface.commit();
        }
        
        if(!window->ignoresMouse) {
            auto inputRegion = compositor.create_region();
            inputRegion.add(0, 0, window->bounds.getWidth(), window->bounds.getHeight());
            window->surface.set_input_region(inputRegion);
        }
    }
    else {
        window->surface.attach(nullptr, 0, 0);
        window->surface.commit();
        if(window->subsurface)
        {
            window->parentWindow->surface.commit();
        }
        
        // Set input region to an empty rect
        window->surface.set_input_region(compositor.create_region());
    }
}

void WaylandWindowSystem::setFullscreen (WaylandWindow* window, bool shouldBeFullscreen)
{
    if (window->frame) {  
        if (shouldBeFullscreen) {   
            libdecor_frame_set_maximized(window->frame);
        } else {
            libdecor_frame_unset_maximized(window->frame);
        }
    }
    window->fullscreen = true;
}
void WaylandWindowSystem::setMinimised (WaylandWindow* window, bool shouldBeMinimised)
{
    // Only supported for top-level windows
    if (shouldBeMinimised && window->frame) {
        libdecor_frame_set_minimized(window->frame);
    }
}

bool WaylandWindowSystem::isFullscreen (WaylandWindow* window)
{
    return window->fullscreen;
}
bool WaylandWindowSystem::isMinimised (WaylandWindow* window)
{
    return window->minimised;
}

void WaylandWindowSystem::grabFocus(WaylandWindow* window)
{
    keyboardFocused = window;
    lastFocusedWindow = window->subsurface ? lastFocusedWindow : window;
}

bool WaylandWindowSystem::isFocused(WaylandWindow* window)
{
    return pointerFocused == window || keyboardFocused == window;
}

void WaylandWindowSystem::blitToWindow(WaylandWindow* window, const Image& image, Rectangle<int> bounds, Rectangle<int> totalArea) {
    if (!window || !window->visible) return;
    
    auto& currentBuffer = window->currentBuffer;
    auto scaleFactor = window->peer->getPlatformScaleFactor();
    int windowWidth = window->bounds.getWidth() * scaleFactor;
    int windowHeight = window->bounds.getHeight() * scaleFactor;
    
    
    if (windowWidth <= 0 || windowHeight <= 0) {
        return;
    }
    
    if (!currentBuffer || currentBuffer->width != windowWidth || currentBuffer->height != windowHeight) {
        try {
            currentBuffer = std::make_unique<WaylandShmBuffer>(windowWidth, windowHeight);
            window->surface.attach(currentBuffer->buffer, 0, 0);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create SHM buffer: " << e.what() << std::endl;
            return;
        }
    }
    
    // Validate buffer
    if (!currentBuffer || !currentBuffer->data) {
        std::cerr << "No valid buffer available" << std::endl;
        return;
    }
    
    Rectangle<int> clampedBounds = bounds.getIntersection(Rectangle<int>(0, 0, windowWidth, windowHeight));
    
    uint32_t* bufferData = static_cast<uint32_t*>(currentBuffer->data);
    const Image::BitmapData srcData(image, Image::BitmapData::readOnly);
    
    const Point<int> srcOffset = clampedBounds.getPosition() - totalArea.getPosition();
    
    for (int y = 0; y < clampedBounds.getHeight(); ++y) {
        const int srcY = srcOffset.y + y;
        const int destY = clampedBounds.getY() + y;
        
        if (srcY >= image.getHeight() || destY >= windowHeight) {
            break;
        }
        
        // Line in source image
        auto* srcRow = srcData.getLinePointer(srcY);
        auto* srcPixel = reinterpret_cast<const uint32_t*>(srcRow) + srcOffset.x;

        uint32_t* destRow = bufferData + (destY * windowWidth) + clampedBounds.getX();
        
        const int copyWidth = std::min(clampedBounds.getWidth(), 
                                     std::min(windowWidth - clampedBounds.getX(),
                                            image.getWidth() - srcOffset.x));
        
        if (copyWidth > 0) {
            std::memcpy(destRow, srcPixel, copyWidth * sizeof(uint32_t));
        }
    }
    
    window->surface.damage_buffer(clampedBounds.getX(), clampedBounds.getY(), clampedBounds.getWidth(), clampedBounds.getHeight());
}


void WaylandWindowSystem::setTitle(WaylandWindow* window, const String& title) {
    // Only toplevel windows have titles
    if (window->frame) {
        libdecor_frame_set_title(window->frame, title.toRawUTF8());
    }
}

wl_surface* WaylandWindowSystem::getSurfaceForWindow(WaylandWindow* window)
{
    return (wl_surface*)window->surface;
}

WaylandWindow* WaylandWindowSystem::findWindowBySurface(const wayland::surface_t& surface) {
    try
    {
        auto it = surfaceToWindow.find(surface.get_id());
        return (it != surfaceToWindow.end()) ? it->second : nullptr;
    }
     catch(...)
    {
        std::cout << "Failed to get surface" << std::endl;
    }
    
    return nullptr;
}

void WaylandWindowSystem::updateMouseModifiers(uint32_t button, bool pressed) {
    auto& mods = ModifierKeys::currentModifiers;
    switch (button) {
        case BTN_LEFT:
            if (pressed) mods = mods.withFlags(ModifierKeys::leftButtonModifier);
            else mods = mods.withoutFlags(ModifierKeys::leftButtonModifier);
            break;
        case BTN_RIGHT:
            if (pressed) mods = mods.withFlags(ModifierKeys::rightButtonModifier);
            else mods = mods.withoutFlags(ModifierKeys::rightButtonModifier);
            break;
        case BTN_MIDDLE:
            if (pressed) mods = mods.withFlags(ModifierKeys::middleButtonModifier);
            else mods = mods.withoutFlags(ModifierKeys::middleButtonModifier);
            break;
    };
}
    
ModifierKeys WaylandWindowSystem::getNativeRealtimeModifiers()
{
    auto mods = ModifierKeys::currentModifiers.withOnlyMouseButtons();
    if (xkbState != nullptr)
    {
        const auto active = XKB_STATE_MODS_EFFECTIVE;

        if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_SHIFT, active))
            mods = mods.withFlags(ModifierKeys::shiftModifier);

        if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_CTRL, active)) {
            mods = mods.withFlags(ModifierKeys::ctrlModifier);
        }
        if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_ALT, active))
            mods = mods.withFlags(ModifierKeys::altModifier);

        if (xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_LOGO, active))
            mods = mods.withFlags(ModifierKeys::commandModifier); // Maps to Cmd on mac, Win key on Linux
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

void WaylandWindowSystem::setupGlobalInput() {
    if (!seat) return;
    
    try {
        globalKeyboard = seat.get_keyboard();
        
        globalKeyboard.on_enter() = [this](uint32_t serial, wayland::surface_t surface, wayland::array_t keys) {
            WaylandWindow* window = findWindowBySurface(surface);
            if (window) {
                keyboardFocused = window;
                if(!lastFocusedWindow) lastFocusedWindow = window->subsurface ? lastFocusedWindow : window; // prefer pointer focus over keyboard focus
            }
        };
        
        globalKeyboard.on_leave() = [this](uint32_t serial, wayland::surface_t surface) {
            WaylandWindow* window = findWindowBySurface(surface);
            if (window && keyboardFocused == window) {
                keyboardFocused = nullptr;
            }
        };
        
        globalKeyboard.on_keymap() = [this](wayland::keyboard_keymap_format format, int32_t fd, uint32_t size) {
            setupKeymap(fd, size);
        };
        
        globalKeyboard.on_key() = [this](uint32_t serial, uint32_t time, uint32_t key, wayland::keyboard_key_state state) {
            if (auto* window = keyboardFocused) {
                handleKeyEvent(window, key, state == wayland::keyboard_key_state::pressed);
            }
        };
        
        globalKeyboard.on_repeat_info() = [this](int32_t rate, int32_t delay) {
            keyRepeatRate = rate;
            keyRepeatDelay = delay;
        };
        
        globalKeyboard.on_modifiers() = [this](uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
            if (auto* window = keyboardFocused) {
                if (xkbState) {
                    xkb_state_update_mask(xkbState, depressed, latched, 
                                         locked, 0, 0, group);
                }
                
                ModifierKeys::currentModifiers = getNativeRealtimeModifiers();
                window->peer->handleModifierKeysChange();
                window->peer->handleKeyUpOrDown (depressed);
            }
        };
        
    } catch (...) {
        std::cerr << "Failed to set up global keyboard" << std::endl;
    }
    
    try {
        globalPointer = seat.get_pointer();
        
        globalPointer.on_enter() = [this](uint32_t serial, wayland::surface_t surface, double x, double y) {
            globalPointer.set_cursor(0, cursorSurface, 0, 0); // force cursor update
            updateCursor();
            if (auto* window = findWindowBySurface(surface)) {
                currentMousePosition = window->peer->localToGlobal(Point<float>(x, y));
                if(!window->ignoresMouse) pointerFocused = window;
                lastFocusedWindow = window->subsurface ? lastFocusedWindow : window;
                handleMouseEvent(window);
            }
        };
        
        globalPointer.on_leave() = [this](uint32_t serial, wayland::surface_t surface) {            
            WaylandWindow* window = findWindowBySurface(surface);
            if (window && pointerFocused == window) {
                pointerFocused = nullptr;
                handleMouseEvent(window);
            }
            
            // Send mouse-up events for any held mouse buttons
            // Since triggering a host managed resize also makes us lose pointer focus
            // sending mouse-ups here will cancel non-managed resizing
            if(window && ModifierKeys::currentModifiers.isLeftButtonDown())
            {
                updateMouseModifiers(BTN_LEFT, false);
                handleMouseEvent(window);
            }
            if(window && ModifierKeys::currentModifiers.isRightButtonDown())
            {
                updateMouseModifiers(BTN_RIGHT, false);
                handleMouseEvent(window);
            }
            if(window && ModifierKeys::currentModifiers.isMiddleButtonDown())
            {
                updateMouseModifiers(BTN_MIDDLE, false);
                handleMouseEvent(window);
            }
        };
        
        globalPointer.on_motion() = [this](uint32_t time, double x, double y) {   
            lastMouseTime = time;
            renderCursor();
            if (auto* window = pointerFocused) {
                currentMousePosition = window->peer->localToGlobal(Point<float>(x, y));
                handleMouseEvent(window);
            }
        };
        
        globalPointer.on_button() = [this](uint32_t serial, uint32_t time, uint32_t button, wayland::pointer_button_state state) {
            currentMouseSerial = serial;
            lastMouseTime = time;
            bool pressed = (state == wayland::pointer_button_state::pressed);
            updateMouseModifiers(button, pressed);
            if (auto* window = pointerFocused) {
                 handleMouseEvent(window);
            }
        };
        
        globalPointer.on_axis() = [this](uint32_t time, wayland::pointer_axis axis, double value) {
            lastMouseTime = time;
            if (auto* window = pointerFocused) {
                auto scrollDistance = value / 64.0f;
                auto isVertical = axis == wayland::pointer_axis::vertical_scroll;
                auto localPos = window->peer->globalToLocal(currentMousePosition);
                
                MouseWheelDetails wheel;
                wheel.deltaX = isVertical ? 0 : -scrollDistance;
                wheel.deltaY = isVertical ? -scrollDistance : 0;
                wheel.isReversed = false;
                wheel.isSmooth = false;
                wheel.isInertial = false;
                window->peer->handleMouseWheel (MouseInputSource::InputSourceType::mouse, localPos, time, wheel);
            }
        };
        
    } catch (...) {
        std::cerr << "Failed to set up global pointer" << std::endl;
    }
}


void WaylandWindowSystem::roundtrip() {
    display->roundtrip();
}

void WaylandWindowSystem::setupKeymap(int fd, uint32_t size) {
    if (!xkbContext || fd < 0 || size == 0 || size > 1024 * 1024) {
        std::cerr << "Error initialising keymap" << std::endl;
        return;
    }

    char* keymapString = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (keymapString == MAP_FAILED) {
        std::cerr << "Failed to mmap keymap: " << strerror(errno) << std::endl;
        return;
    }
    
    std::string keymapStr;
    if (size > 0 && keymapString[size - 1] == '\0') {
        keymapStr = std::string(keymapString, size - 1);
    } else {
        keymapStr = std::string(keymapString, size);
    }
    
    if (keymap) {
        xkb_keymap_unref(keymap);
        keymap = nullptr;
    }
    
    keymap = xkb_keymap_new_from_string(xkbContext, keymapStr.c_str(), 
                                       XKB_KEYMAP_FORMAT_TEXT_V1, 
                                       XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    if (munmap(keymapString, size) != 0) {
        std::cerr << "Warning: Failed to unmap keymap memory: " << strerror(errno) << std::endl;
    }
    
    if (!keymap) {
        std::cerr << "Failed to create XKB keymap from string" << std::endl;
        return;
    }
    
    if (xkbState) {
        xkb_state_unref(xkbState);
        xkbState = nullptr;
    }
    
    xkbState = xkb_state_new(keymap);
    if (!xkbState) {
        std::cerr << "Failed to create XKB state" << std::endl;
        return;
    }
}

void WaylandWindowSystem::handleMouseEvent(WaylandWindow* window) {
    if(window->ignoresMouse || window != pointerFocused)
    {
        return;
    }
    
    auto localPos = window->peer->globalToLocal(currentMousePosition);
    window->peer->handleMouseEvent(MouseInputSource::InputSourceType::mouse,
                   localPos,
                   ModifierKeys::currentModifiers,
                   MouseInputSource::defaultPressure,  // Default pressure
                   MouseInputSource::defaultOrientation, // Default orientation  
                   lastMouseTime);
}

void WaylandWindowSystem::handleKeyEvent(WaylandWindow* window, uint32_t waylandKey, bool pressed) {    
    int xkbKeyCode = waylandKey + 8;
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkbState, xkbKeyCode);
    
    auto oldMods = ModifierKeys::currentModifiers;
    ModifierKeys::currentModifiers = getNativeRealtimeModifiers();
    
    int juceKeyCode = keysym;
    bool isSpecialKey = false;
    
    // Only process special keys (function keys, navigation keys, etc.)
    if ((keysym & 0xff00) == 0xff00) {
        isSpecialKey = true;
        
        // First translate keypad keys to their regular equivalents
        switch (keysym) {
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
        }
        
        // Then convert to JUCE key codes
        switch (keysym) {
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
    if (pressed) {
        int ret = xkb_state_key_get_utf8(xkbState, xkbKeyCode, buffer, sizeof(buffer));
        if (ret > 0 && ret < sizeof(buffer)) {
            if (ret == 1) {
                textChar = static_cast<juce_wchar>(buffer[0]);
            }
        }
    }
    
    bool shouldSendKeyPress = (textChar != 0) || isSpecialKey;
    
    if (oldMods != ModifierKeys::currentModifiers)
        window->peer->handleModifierKeysChange();

    window->peer->handleKeyUpOrDown(pressed);

    if (shouldSendKeyPress && pressed) {
        window->peer->handleKeyPress(juceKeyCode, textChar);
    }
    
    if(pressed) {
        lastKey = {juceKeyCode, textChar};
        keysCurrentlyDown.insert(juceKeyCode);
        keyRepeater.startTimer(keyRepeatDelay);
    }
    else {
        keysCurrentlyDown.erase(juceKeyCode);  
       if(keysCurrentlyDown.empty()) {
            keyRepeater.stopTimer();
       }
    }
}

void WaylandWindowSystem::handleKeyRepeat()
{
    if(keyboardFocused) {
        keyboardFocused->peer->handleKeyPress(lastKey.first, lastKey.second);
        keyRepeater.startTimer(keyRepeatRate);
    }
}

void WaylandWindowSystem::commit(WaylandWindow* window) {
    window->surface.commit();
}

 // Add this method to get display information
Array<Displays::Display> WaylandWindowSystem::findDisplays(float masterScale) {
    Array<Displays::Display> displays;
    
    // Do a roundtrip to ensure we get all output information
    roundtrip();
    
    // Wait a bit more for all outputs to report "done"
    int timeout = 100; // 1 second timeout
    bool all_done = false;
    while (!all_done && timeout-- > 0) {
        {
            all_done = std::all_of(outputs.begin(), outputs.end(),
                [](const auto& output) { return output->done; });
        }
        
        if (!all_done) {
            usleep(10000); // 10ms
            display->dispatch_pending();
        }
    }
    
    if (outputs.empty()) {
        // Fallback: create a default display
        Displays::Display d;
        d.totalArea = Rectangle<int>(0, 0, 1920, 1080); // Default size
        d.userArea = d.totalArea;
        d.isMain = true;
        d.scale = masterScale;
        d.dpi = 96.0; // Default DPI
        displays.add(d);
        return displays;
    }
    
    // Determine which output should be primary
    // In Wayland, there's no explicit "primary" output, so we'll use heuristics:
    // 1. Output at position (0,0)
    // 2. If none at (0,0), use the first one
    auto primary_it = std::find_if(outputs.begin(), outputs.end(),
        [](const auto& output) { return output->x == 0 && output->y == 0; });
    
    if (primary_it == outputs.end() && !outputs.empty()) {
        outputs[0]->is_primary = true;
    } else if (primary_it != outputs.end()) {
        (*primary_it)->is_primary = true;
    }
    
    // Convert Wayland outputs to JUCE Display objects
    for (const auto& waylandOutput : outputs) {
        if (!waylandOutput->done || waylandOutput->width <= 0 || waylandOutput->height <= 0) {
            continue;
        }
        
        Displays::Display d;
        d.totalArea = Rectangle<int>(waylandOutput->x, waylandOutput->y, 
                                   waylandOutput->width, waylandOutput->height);
        d.userArea = d.totalArea; // Wayland doesn't expose workarea directly
        d.isMain = waylandOutput->is_primary;
        
        // Calculate DPI from physical dimensions
        if (waylandOutput->physical_width > 0 && waylandOutput->physical_height > 0) {
            // Convert mm to inches and calculate DPI
            double width_inches = waylandOutput->physical_width / 25.4;
            double height_inches = waylandOutput->physical_height / 25.4;
            double dpi_x = waylandOutput->width / width_inches;
            double dpi_y = waylandOutput->height / height_inches;
            d.dpi = (dpi_x + dpi_y) * 0.5; // Average of x and y DPI
        } else {
            d.dpi = 96.0; // Fallback DPI
            std::cout << "Warning: No physical dimensions for output " 
                     << waylandOutput->name << ", using default DPI" << std::endl;
        }
        
        // Apply Wayland's scale factor and master scale
        d.scale = masterScale * waylandOutput->scale_factor;
        
        // Set refresh rate if available
        if (waylandOutput->refresh_rate > 0) {
            d.verticalFrequencyHz = waylandOutput->refresh_rate / 1000.0; // Convert mHz to Hz
        }
        
        // Add primary display first
        if (d.isMain) {
            displays.insert(0, d);
        } else {
            displays.add(d);
        }
    }
    
    // Ensure we have at least one display marked as main
    if (!displays.isEmpty() && !displays.getReference(0).isMain) {
        displays.getReference(0).isMain = true;
    }
    
    return displays;
}

void WaylandWindowSystem::updateCursor()
{        
    if(!cursorSurface) {
        cursorTheme = wayland::cursor_theme_t("", 24, (wl_shm*)shm);
        loadStandardCursors();
        cursorSurface = compositor.create_surface();
    }
    
    if(currentCursor.standardCursor == -1)
    {
        auto& shm = currentCursor.cursorImage;
        if(shm->buffer) {
            cursorSurface.attach(shm->buffer, 0, 0);
            cursorSurface.damage(0, 0, shm->width, shm->height);
            cursorSurface.commit();
            globalPointer.set_cursor(0, cursorSurface, shm->hotspotX, shm->hotspotY);
        }
    }
    else if(currentCursor.standardCursor == MouseCursor::NoCursor)
    {
        globalPointer.set_cursor(0, nullptr, 0, 0);
    }
    else if(currentCursor.standardCursor >= 0 && currentCursor.standardCursor < 20) {
        wayland::cursor_t cursor = standardCursors[currentCursor.standardCursor];
        auto cursorImage = cursor.image(0);
        auto cursorBuffer = cursorImage.get_buffer();
        cursorSurface.attach(cursorBuffer, 0, 0);
        cursorSurface.damage(0, 0, cursorImage.width(), cursorImage.height());
        cursorSurface.commit();
        globalPointer.set_cursor(0, cursorSurface, cursorImage.hotspot_x(), cursorImage.hotspot_y());
    }
}

void WaylandWindowSystem::renderCursor()
{
    if (!shm) return;
    
    cursorFrameCallback = cursorSurface.frame();
    cursorFrameCallback.on_done() = [this](uint32_t){
        updateCursor();
    };
    
    cursorSurface.commit();
}

void WaylandWindowSystem::loadStandardCursors()
{
    if (!cursorTheme) return;
    
    // Map cursor types to Wayland cursor names
    struct CursorMapping {
        int type;
        const char* name;
    };
    
    const CursorMapping mappings[] = {
    {MouseCursor::ParentCursor, "default"},
    {MouseCursor::NoCursor, nullptr},
    {MouseCursor::NormalCursor, "default"},
    {MouseCursor::WaitCursor, "wait"},
    {MouseCursor::IBeamCursor, "text"},
    {MouseCursor::CrosshairCursor, "crosshair"},
    {MouseCursor::CopyingCursor, "copy"},
    {MouseCursor::PointingHandCursor, "pointer"},
    {MouseCursor::DraggingHandCursor, "grab"},
    {MouseCursor::LeftRightResizeCursor, "col-resize"},
    {MouseCursor::UpDownResizeCursor, "row-resize"},
    {MouseCursor::UpDownLeftRightResizeCursor, "all-scroll"},
    {MouseCursor::TopEdgeResizeCursor, "n-resize"},
    {MouseCursor::BottomEdgeResizeCursor, "s-resize"},
    {MouseCursor::LeftEdgeResizeCursor, "w-resize"},
    {MouseCursor::RightEdgeResizeCursor, "e-resize"},
    {MouseCursor::TopLeftCornerResizeCursor, "nw-resize"},
    {MouseCursor::TopRightCornerResizeCursor, "ne-resize"},
    {MouseCursor::BottomLeftCornerResizeCursor, "sw-resize"},
    {MouseCursor::BottomRightCornerResizeCursor, "se-resize"}
    };
    
    for (const auto& mapping : mappings) {
        if (mapping.name) {
            wayland::cursor_t cursor = cursorTheme.get_cursor(mapping.name);
            if (cursor) {
                standardCursors[mapping.type] = cursor;
            } else {
                // Fallback to default cursor
                standardCursors[mapping.type] = cursorTheme.get_cursor("default");
            }
        }
    }
}


WaylandCursor WaylandWindowSystem::createCustomMouseCursorInfo(const Image& image, Point<int> hotspot)
{
    WaylandCursor cursor;
    cursor.standardCursor = -1;
    
    auto imageW = (unsigned int) image.getWidth();
    auto imageH = (unsigned int) image.getHeight();
    
    // Create shared memory buffer for cursor
    cursor.cursorImage = new WaylandShmBuffer(imageW, imageH);
    
    // Copy image data to buffer
    uint32_t* bufferData = static_cast<uint32_t*>(cursor.cursorImage->data);
    const Image::BitmapData srcData(image, Image::BitmapData::readOnly);
    
    for (int y = 0; y < (int)imageH; ++y) {
        auto* srcRow = srcData.getLinePointer(y);
        auto* srcPixel = reinterpret_cast<const uint32_t*>(srcRow);
        uint32_t* destRow = bufferData + (y * imageW);
        
        for (int x = 0; x < (int)imageW; ++x) {
            destRow[x] = srcPixel[x];
        }
    }
    
    cursor.cursorImage->hotspotX = hotspot.x;
    cursor.cursorImage->hotspotY = hotspot.y;
    
    return cursor;
}

void WaylandWindowSystem::deleteMouseCursor(WaylandCursor cursorHandle)
{
    if(cursorHandle.standardCursor == -1) {
        delete cursorHandle.cursorImage;
    }
}

WaylandCursor WaylandWindowSystem::createStandardMouseCursor(MouseCursor::StandardCursorType type)
{    
    WaylandCursor cursor;
    cursor.standardCursor = (int)type;
    cursor.cursorImage = nullptr;
    return cursor;
}

void WaylandWindowSystem::showCursor(WaylandCursor cursorHandle)
{
    currentCursor = cursorHandle;
    updateCursor();
}

void WaylandWindowSystem::initializeDataDeviceManager()
{
    // This should be called in your constructor after registry setup
    if (dataDeviceManager)
    {
        dataDevice = dataDeviceManager.get_data_device(seat);
        setupDataDeviceCallbacks();
    }
}

void WaylandWindowSystem::setupDataDeviceCallbacks()
{
    dataDevice.on_data_offer() = [this](wayland::data_offer_t offer) {
        currentOffer = offer;
        availableMimeTypes.clear();
        hasFiles = hasText = false;
        
        offer.on_offer() = [this](const std::string& mimeType) {
            availableMimeTypes.push_back(mimeType);
            if (mimeType == "text/uri-list") hasFiles = true;
            else if (mimeType == "text/plain" || mimeType == "text/plain;charset=utf-8") hasText = true;
        };
    };
    
    dataDevice.on_enter() = [this](uint32_t serial, wayland::surface_t surface, 
                                  double x, double y, wayland::data_offer_t offer) {
        dragTargetWindow = findWindowBySurface(surface);
        currentOffer = offer;
        currentDragInfo = ComponentPeer::DragInfo{}; // Reset
        
        if (!dragTargetWindow || !dragTargetWindow->peer) return;
        
        // Accept the offer first so we can read data
        if (hasFiles) {
            offer.accept(serial, "text/uri-list");
            offer.set_actions(wayland::data_device_manager_dnd_action::copy | 
                             wayland::data_device_manager_dnd_action::move,
                             wayland::data_device_manager_dnd_action::copy);
            
            // Read file data immediately
            readOfferData("text/uri-list", [this, x, y](const String& data) {
                currentDragInfo.files.clear();
                auto lines = StringArray::fromLines(data);
                for (const auto& line : lines) {
                    String trimmed = line.trim();
                    if (trimmed.startsWith("file://")) {
                        currentDragInfo.files.add(URL::removeEscapeChars(trimmed.substring(7)));
                    }
                }
                currentDragInfo.position = Point<int>(x, y);
                dragTargetWindow->peer->handleDragMove(currentDragInfo);
            });
        } else if (hasText) {
            offer.accept(serial, "text/plain");  
            offer.set_actions(wayland::data_device_manager_dnd_action::copy,
                             wayland::data_device_manager_dnd_action::copy);
                             
            // Read text data immediately
            readOfferData("text/plain", [this, x, y](const String& data) {
                currentDragInfo.text = data;
                currentDragInfo.position = Point<int>(x, y);
                dragTargetWindow->peer->handleDragMove(currentDragInfo);
            });
        }
    };
    
    dataDevice.on_motion() = [this](uint32_t time, double x, double y) {        
        if (!dragTargetWindow || !dragTargetWindow->peer) return;
        
        // Use cached data with updated position
        currentDragInfo.position = Point<int>(x, y);
        bool accepted = dragTargetWindow->peer->handleDragMove(currentDragInfo);
        
        if (!accepted) {
            // Could reject the offer here if needed
        }
    };
    
    dataDevice.on_drop() = [this]() {
        if (!dragTargetWindow || !dragTargetWindow->peer) return;
        
        // Use the cached data for the drop
        bool accepted = dragTargetWindow->peer->handleDragDrop(currentDragInfo);
        if (accepted && currentOffer) {
            currentOffer.finish();
        }
    };
    
    dataDevice.on_leave() = [this]() {
        if (dragTargetWindow && dragTargetWindow->peer) {
            dragTargetWindow->peer->handleDragExit(currentDragInfo);
        }
        dragTargetWindow = nullptr;
        currentOffer = wayland::data_offer_t{}; // Reset
    };
}

// Read data from offer
void WaylandWindowSystem::readOfferData(const std::string& mimeType, std::function<void(const String&)> callback) {
    if (!currentOffer) {
        callback("");
        return;
    }

    int pipeFds[2];
    if (pipe(pipeFds) == -1) {
        callback("");
        return;
    }

    currentOffer.receive(mimeType, pipeFds[1]);
    close(pipeFds[1]);

    roundtrip(); // Request flush of send buffers

    // Wait for data to become available
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(pipeFds[0], &fds);
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int ret = select(pipeFds[0] + 1, &fds, nullptr, nullptr, &timeout);
    String data;

    if (ret > 0 && FD_ISSET(pipeFds[0], &fds)) {
        char buffer[4096];
        ssize_t bytesRead;
        
        // Set non-blocking
        int flags = fcntl(pipeFds[0], F_GETFL, 0);
        fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK);
        
        while ((bytesRead = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
            data += String::createStringFromData(buffer, static_cast<int>(bytesRead));
        }
    }

    close(pipeFds[0]);
    callback(data);
}

void WaylandWindowSystem::copyTextToClipboard(const String& text)
{
    if (!dataDeviceManager || text.isEmpty())
        return;
    
    currentClipboardText = text;
    
    // Create a new data source
    clipboardSource = dataDeviceManager.create_data_source();
    
    // Add text/plain mime type
    clipboardSource.offer("text/plain");
    clipboardSource.offer("text/plain;charset=utf-8");
    clipboardSource.offer("UTF8_STRING");
    
    // Set up callbacks
    clipboardSource.on_send() = [this](const std::string& mimeType, int fd) {
        handleDataSourceSend(clipboardSource, mimeType, fd);
    };
    
    clipboardSource.on_cancelled() = [this]() {
        handleDataSourceCancelled(clipboardSource);
    };
    
    // Set the selection
    dataDevice.set_selection(clipboardSource, currentMouseSerial);
    hasClipboardData = true;
}

String WaylandWindowSystem::getTextFromClipboard() const
{
    return currentClipboardText;
}

bool WaylandWindowSystem::externalDragFileInit(WaylandWindow* window, const StringArray& files, 
                                        bool canMoveFiles, std::function<void()>&& callback)
{
    if (!dataDeviceManager || !window || files.isEmpty())
        return false;
    
    dragWindow = window;
    dragFiles = files;
    dragCanMoveFiles = canMoveFiles;
    dragCompletionCallback = std::move(callback);
    
    // Create data source for drag
    dragSource = dataDeviceManager.create_data_source();
    
    // Offer file URI list
    dragSource.offer("text/uri-list");
    dragSource.offer("text/plain");
    
    setupDragSource(files, {}, canMoveFiles);

    // Start the drag
    dataDevice.start_drag(dragSource, window->surface, nullptr, currentMouseSerial);
    return true;
}

bool WaylandWindowSystem::externalDragTextInit(WaylandWindow* window, const String& text, 
                                        std::function<void()>&& callback)
{
    if (!dataDeviceManager || !window || text.isEmpty())
        return false;
    
    dragWindow = window;
    dragText = text;
    dragCompletionCallback = std::move(callback);
    
    // Create data source for drag
    dragSource = dataDeviceManager.create_data_source();
    
    // Offer text types
    dragSource.offer("text/plain");
    dragSource.offer("text/plain;charset=utf-8");
    dragSource.offer("UTF8_STRING");
    
    setupDragSource({}, text, false);
    
    dataDevice.start_drag(dragSource, window->surface, nullptr, currentMouseSerial);
    return true;
}

void WaylandWindowSystem::setupDragSource(const StringArray& files, const String& text, bool canMoveFiles)
{
    dragSource.on_send() = [this](const std::string& mimeType, int fd) {
        handleDataSourceSend(dragSource, mimeType, fd);
    };
    
    dragSource.on_cancelled() = [this]() {
        handleDataSourceCancelled(dragSource);
    };
    
    dragSource.on_dnd_drop_performed() = [this]() {
        dragWindow = nullptr;
        if (dragCompletionCallback)
            dragCompletionCallback();
        dragCompletionCallback = nullptr;
    };
    
    dragSource.on_dnd_finished() = [this]() {
        dragWindow = nullptr;
        if (dragCompletionCallback)
            dragCompletionCallback();
        dragCompletionCallback = nullptr;
    };

    // Set supported actions
    if (canMoveFiles)
        dragSource.set_actions(wayland::data_device_manager_dnd_action::move | wayland::data_device_manager_dnd_action::copy);
    else
        dragSource.set_actions(wayland::data_device_manager_dnd_action::copy);
}

void WaylandWindowSystem::handleDataSourceSend(wayland::data_source_t source, 
                                               const std::string& mimeType, int fd)
{
    String dataToSend;
    
    if (source == clipboardSource)
    {
        dataToSend = currentClipboardText;
    }
    else if (source == dragSource)
    {
        if (mimeType == "text/uri-list" && !dragFiles.isEmpty())
        {
            // Convert file paths to URI list
            StringArray uris;
            for (const auto& file : dragFiles)
            {
                uris.add("file://" + file);
            }
            dataToSend = uris.joinIntoString("\r\n") + "\r\n";
        }
        else if (mimeType.find("text/") == 0)
        {
            dataToSend = dragText.isNotEmpty() ? dragText : dragFiles.joinIntoString("\n");
        }
    }
    
    writeDataToFd(fd, dataToSend);
}

void WaylandWindowSystem::writeDataToFd(int fd, const String& data)
{
    auto utf8 = data.toUTF8();
    ssize_t written = 0;
    size_t total = utf8.sizeInBytes() - 1; // Exclude null terminator
    
    while (written < static_cast<ssize_t>(total))
    {
        ssize_t result = write(fd, utf8.getAddress() + written, total - written);
        if (result < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            break;
        }
        written += result;
    }
    
    close(fd);
}

void WaylandWindowSystem::handleDataSourceCancelled(wayland::data_source_t source)
{
    if (source == dragSource)
    {
        dragWindow = nullptr;
        if (dragCompletionCallback)
            dragCompletionCallback();
        dragCompletionCallback = nullptr;
    }
    else if (source == clipboardSource)
    {
        hasClipboardData = false;
    }
}

bool WaylandWindowSystem::isKeyCurrentlyDown(int keyCode)
{
    return keysCurrentlyDown.find(keyCode) != keysCurrentlyDown.cend();
}

JUCE_IMPLEMENT_SINGLETON (WaylandWindowSystem)

} // namespace juce



