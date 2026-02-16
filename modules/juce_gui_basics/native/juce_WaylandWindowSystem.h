/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
 */

namespace juce
{

struct WaylandWindow;
struct WaylandDisplay;
struct WaylandShmBuffer;
struct WaylandCursor
{
    int standardCursor;
    WaylandShmBuffer* cursorImage;
};

template<typename TypeID>
class MultiTouchMapper;

enum class MouseEventFlags
{
    none,
    down,
    up,
    upAndCancel
};

class WaylandWindowSystem : public DeletedAtShutdown
{
public:
    bool isWaylandAvailable();
    bool isTouchAvailable();
    
    ModifierKeys getNativeRealtimeModifiers();
    Point<float> getCurrentMousePosition();
    
    // Window management
    WaylandWindow* findWindowBySurface (wl_surface* surface);
    WaylandWindow* createWindow (bool isSubsurface, ComponentPeer* peer, bool alwaysOnTop, WaylandWindow* parent = nullptr);
    void destroyWindow (WaylandWindow* window);
    void setBounds (WaylandWindow* window, Rectangle<int> bounds);
    Rectangle<int> getBounds (WaylandWindow* window);
    void blitToWindow (WaylandWindow* window, const juce::Image& image, Rectangle<int> bounds, Rectangle<int> totalArea);
    void setTitle (WaylandWindow* window, const String& title);
    WaylandWindow* getWaylandWindowForPeer (ComponentPeer* peer);
    void startHostManagedResize (WaylandWindow* window, ResizableBorderComponent::Zone zone);
    void toFront (WaylandWindow* window, bool enforce);
    void toBehind (WaylandWindow* window, WaylandWindow* otherWindow);
    void setVisible (WaylandWindow* window, bool shouldBeVisible);
    void setFullscreen (WaylandWindow* window, bool shouldBeFullscreen);
    void setMinimised (WaylandWindow* window, bool shouldBeMinimised);
    bool isFullscreen (WaylandWindow* window);
    bool isMinimised (WaylandWindow* window);
    void updateConstraints (WaylandWindow* window) const;
    
    bool isKeyCurrentlyDown (int keyCode);
    
    wl_surface* getSurfaceForWindow (WaylandWindow* window);
    void commit (WaylandWindow* window);
    void requestFrame (WaylandWindow* window);
    
    // Cursor interface
    WaylandCursor createCustomMouseCursorInfo (const Image& image, Point<int> hotspot);
    void deleteMouseCursor (WaylandCursor cursorHandle);
    WaylandCursor createStandardMouseCursor (MouseCursor::StandardCursorType cursor);
    void showCursor (WaylandCursor cursorHandle);
    
    // Drag and drop interface
    bool externalDragFileInit (WaylandWindow*, const StringArray& files, bool canMove, std::function<void()>&& callback);
    bool externalDragTextInit (WaylandWindow*, const String& text, std::function<void()>&& callback);
    
    // Clipboard interface
    void copyTextToClipboard (const String&);
    String getTextFromClipboard() const;
    
    wl_display* getDisplay() { return display; }
    wl_compositor* getCompositor() { return compositor; }
    wl_shm* getShm() { return shm; }
    
    void grabFocus (WaylandWindow* window);
    bool isFocused (WaylandWindow* window);
    
    Array<Displays::Display> findDisplays (float masterScale);
    int getScaleFactorForWindow (WaylandWindow* window);
    
private:
    WaylandWindowSystem();
    ~WaylandWindowSystem() override;

    // Setup global callbacks
    void setupGlobalInput();
    void setupDisplay (wl_output* output);
    void setupKeymap (int fd, uint32_t size);
    void setupDataDeviceCallbacks();
    
    void updateRegions (WaylandWindow* window) const;
    
    // Cursor internals
    void updateCursor();
    void loadStandardCursors();
    
    // Drag and drop helpers
    void setupDragSource (WaylandWindow* window, bool isText, bool canMoveFiles);
    void writeDataToFd (int fd, const String& data) const;
    void readOfferData (const String& mimeType, std::function<void (const String&)> callback) const            ;
    
    // Z-order management
    void enforceOrder();
    std::vector<WaylandWindow*> getSubsurfacesForParent (WaylandWindow* parent) const;
    auto findInZOrder (WaylandWindow* window) { return std::find (zOrder.begin(), zOrder.end(), window); }
    
    // Input event handling
    void handleMouseEvent (WaylandWindow* window);
    void handleTouchEvent (WaylandWindow* window, Point<float> localPos, int touchIndex, MouseEventFlags eventType);
    void handleKeyEvent (WaylandWindow* window, uint32_t waylandKey, bool pressed);
    void handleKeyRepeat();
    void updateMouseModifiers (uint32_t button, bool pressed);
    
    wl_display* display;
    wl_registry* registry;
    wl_compositor* compositor;
    wl_subcompositor* subcompositor;
    wl_shm* shm;
    wl_seat* seat;
    libdecor* decorator = nullptr;
    
    xkb_state* xkbState = nullptr;
    xkb_context* xkbContext = nullptr;
    xkb_keymap* keymap = nullptr;
    
    wl_cursor_theme* cursorTheme = nullptr;
    wl_surface* cursorSurface = nullptr;
    wl_callback* cursorFrameCallback = nullptr;
    WaylandCursor currentCursor;
    std::map<int, wl_cursor*> standardCursors;
    std::map<void*, std::unique_ptr<WaylandShmBuffer>> customCursors;
    
    std::unordered_map<wl_surface*, WaylandWindow*> surfaceToWindow;
    
    std::vector<WaylandDisplay> displays;
    std::vector<WaylandWindow*> zOrder;
    
    // Input focus tracking
    WaylandWindow* keyboardFocused = nullptr;
    WaylandWindow* pointerFocused = nullptr;
    WaylandWindow* lastFocusedWindow = nullptr;
    
    // Global input objects
    wl_keyboard* globalKeyboard;
    wl_pointer* globalPointer;
    wl_touch* globalTouch;
    
    TimedCallback keyRepeater = TimedCallback ([this](){ handleKeyRepeat(); });
    std::pair<int, juce_wchar> lastKey;
    std::set<int> keysCurrentlyDown;
    int keyRepeatRate, keyRepeatDelay;
    
    Point<float> currentMousePosition;
    uint32_t currentMouseSerial = 0;
    uint32_t currentMouseTime = 0;
    
    uint32_t currentTouchTime = 0;
    std::unique_ptr<MultiTouchMapper<int32>> currentTouches;
    bool hasTouch = false;
    bool hasMouse = false;
    bool hasKeyboard = false;
    
    wl_data_device_manager* dataDeviceManager;
    wl_data_device* dataDevice;
    wl_data_source* dragSource;
    wl_data_source* clipboardSource;
    
    // For sending drag and drop
    std::function<void()> dragCompletionCallback;
    StringArray dragFiles;
    String dragText;
    
    // For receiving drag and drop
    ComponentPeer::DragInfo currentDragInfo;
    wl_data_offer* currentOffer;
    WaylandWindow* dragTargetWindow = nullptr;
    bool isDraggingText = false;
    bool isDraggingFiles = false;
  
    // Clipboard state
    String currentClipboardText;
    bool hasClipboardData = false;
  
    bool initialised = false;
  
public:
    JUCE_DECLARE_SINGLETON_INLINE (WaylandWindowSystem, false)
};

} // namespace juce
