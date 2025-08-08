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

namespace juce
{

// Forward declarations
class WaylandWindow;
class WaylandOutput;
class WaylandComponentPeer;
class WaylandShmBuffer;
class WaylandKeyRepeater;
struct WaylandCursor
{
    int standardCursor;
    WaylandShmBuffer* cursorImage;
};

class WaylandWindowSystem : public DeletedAtShutdown
{
    std::unique_ptr<wayland::display_t> display; // ptr because it throws on construction, which doesn't gel with our singleton setup
    wayland::registry_t registry;
    wayland::compositor_t compositor;
    wayland::subcompositor_t subcompositor;
    wayland::shm_t shm;
    wayland::seat_t seat;
    libdecor* decorator = nullptr;
    static libdecor_interface decorInterface;
    
    xkb_state* xkbState = nullptr;
    xkb_context* xkbContext = nullptr;
    xkb_keymap* keymap = nullptr;
  
    wayland::cursor_theme_t cursorTheme;
    wayland::surface_t cursorSurface;
    wayland::callback_t cursorFrameCallback;
    WaylandCursor currentCursor;
    std::map<int, wayland::cursor_t> standardCursors;
    std::map<void*, std::unique_ptr<WaylandShmBuffer>> customCursors;

    std::unordered_map<uint32_t, WaylandWindow*> surfaceToWindow;
  
    std::vector<std::unique_ptr<WaylandOutput>> outputs;
  
    std::vector<WaylandWindow*> zOrder; 
    
    // Input focus tracking
    WaylandWindow* keyboardFocused = nullptr;
    WaylandWindow* pointerFocused = nullptr;
    WaylandWindow* lastFocusedWindow = nullptr;
    
    // Global input objects
    wayland::keyboard_t globalKeyboard;
    wayland::pointer_t globalPointer;
  
    uint32_t lastMouseTime = 0;
  
    TimedCallback keyRepeater = TimedCallback([this](){ handleKeyRepeat(); });
    std::pair<int, juce_wchar> lastKey;
    std::set<int> keysCurrentlyDown;
    int keyRepeatRate, keyRepeatDelay;
  
    bool initialised = false;
    Point<float> currentMousePosition;
    uint32_t currentMouseSerial;
  
    wayland::data_device_manager_t dataDeviceManager;
    wayland::data_device_t dataDevice;
    wayland::data_source_t dragSource;
    wayland::data_source_t clipboardSource;
    
    // For sending drag and drop
    WaylandWindow* dragWindow = nullptr;
    std::function<void()> dragCompletionCallback;
    StringArray dragFiles;
    String dragText;
    bool dragCanMoveFiles = false;
  
    // For receiving drag and drop
    ComponentPeer::DragInfo currentDragInfo;
    wayland::data_offer_t currentOffer;
    std::vector<std::string> availableMimeTypes;
    WaylandWindow* dragTargetWindow = nullptr;
    bool hasFiles = false;
    bool hasText = false;
    
    // Clipboard state
    String currentClipboardText;
    bool hasClipboardData = false;
    
    WaylandWindowSystem();
    ~WaylandWindowSystem();
    
    void setupGlobalInput();
    void setupKeymap(int fd, uint32_t size);
  
    void handleKeyPress(uint32_t waylandKey, bool pressed);
    int convertKeysym(xkb_keysym_t keysym);
    void updateMouseModifiers(uint32_t button, bool pressed);
    
    // Cursor internals
    void renderCursor();
    void updateCursor();
    void loadStandardCursors();
    wayland::cursor_t getStandardCursor(int cursorType);
  
    void initializeDataDeviceManager();
    void setupDataDeviceCallbacks();
    void handleDataSourceSend(wayland::data_source_t source, const std::string& mimeType, int fd);
    void handleDataSourceCancelled(wayland::data_source_t source);
  
    // Drag and drop helpers
    void setupDragSource(const StringArray& files, const String& text, bool canMoveFiles);
    void writeDataToFd(int fd, const String& data);
    void readOfferData(const std::string& mimeType, std::function<void(const String&)> callback);
  
    void enforceOrder();
    std::vector<WaylandWindow*> getSubsurfacesForParent(WaylandWindow* parent) const;
    auto findInZOrder(WaylandWindow* window) { return std::find(zOrder.begin(), zOrder.end(), window); }
  
    void handleMouseEvent(WaylandWindow* window);
    void handleKeyEvent(WaylandWindow* window, uint32_t waylandKey, bool pressed);
    void handleKeyRepeat();
  
public:
    bool isWaylandAvailable();
  
    ModifierKeys getNativeRealtimeModifiers();
    Point<float> getCurrentMousePosition();
    
    // Window management
    WaylandWindow* findWindowBySurface(const wayland::surface_t& surface);
    WaylandWindow* createWindow(bool isSubsurface, ComponentPeer* peer, WaylandWindow* parent = nullptr);
    void destroyWindow(WaylandWindow* window);
    void setBounds(WaylandWindow* window, Rectangle<int> bounds);
    Rectangle<int> getBounds(WaylandWindow* window);
    void blitToWindow(WaylandWindow* window, const juce::Image& image, Rectangle<int> bounds, Rectangle<int> totalArea);
    void setTitle(WaylandWindow* window, String const& title);
    WaylandWindow* getWaylandWindowForPeer(ComponentPeer* peer);
    void startHostManagedResize(WaylandWindow* window, ResizableBorderComponent::Zone zone);
    void toFront(WaylandWindow* window);
    void toBehind(WaylandWindow* window, WaylandWindow* otherWindow);
    void setVisible (WaylandWindow* window, bool shouldBeVisible);
    void setFullscreen (WaylandWindow* window, bool shouldBeFullscreen);
    void setMinimised (WaylandWindow* window, bool shouldBeMinimised);
    bool isFullscreen (WaylandWindow* window);
    bool isMinimised (WaylandWindow* window);
  
    bool isKeyCurrentlyDown(int keyCode);
  
    // Used for openGL
    wl_surface* getSurfaceForWindow(WaylandWindow* window);
    void commit(WaylandWindow* window);
  
    void requestFrame(WaylandWindow* window);
  
    // Cursor interface
    WaylandCursor createCustomMouseCursorInfo(const Image& image, Point<int> hotspot);
    void deleteMouseCursor(WaylandCursor cursorHandle);
    WaylandCursor createStandardMouseCursor(MouseCursor::StandardCursorType cursor);
    void showCursor(WaylandCursor cursorHandle);
  
    bool externalDragFileInit (WaylandWindow*, const StringArray& files, bool canMove, std::function<void()>&& callback);
    bool externalDragTextInit (WaylandWindow*, const String& text, std::function<void()>&& callback);
    void copyTextToClipboard (const String&);
    String getTextFromClipboard() const;
  
  
    wayland::display_t& getDisplay() { return *display; }
    wayland::compositor_t& getCompositor() { return compositor; }
    wayland::shm_t& getShm() { return shm; }
    libdecor* getDecorator() { return decorator; }
                                                           
    void roundtrip();
  
    void grabFocus(WaylandWindow* window);
    bool isFocused(WaylandWindow* window);
  
    Array<Displays::Display> findDisplays(float masterScale);

    JUCE_DECLARE_SINGLETON (WaylandWindowSystem, false)
};

} // namespace juce
