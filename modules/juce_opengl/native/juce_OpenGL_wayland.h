/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
 */

namespace juce
{

struct WaylandEGL
{
    static bool initialise()
    {
        if (isInitialised) return true;
        
        bool loadedWaylandEgl = loadSymbol (waylandEglLib, "wl_egl_window_create", wlWindowCreate)
        && loadSymbol (waylandEglLib, "wl_egl_window_destroy", wlWindowDestroy)
        && loadSymbol (waylandEglLib, "wl_egl_window_resize", wlWindowResize)
        && loadSymbol (waylandEglLib, "wl_egl_window_get_attached_size", wlWindowGetAttachedSize);
        
        bool loadedEgl = loadSymbol (eglLib, "eglGetDisplay", eglGetDisplay)
        && loadSymbol (eglLib, "eglInitialize", eglInitialize)
        && loadSymbol (eglLib, "eglChooseConfig", eglChooseConfig)
        && loadSymbol (eglLib, "eglCreateWindowSurface", eglCreateWindowSurface)
        && loadSymbol (eglLib, "eglCreateContext", eglCreateContext)
        && loadSymbol (eglLib, "eglMakeCurrent", eglMakeCurrent)
        && loadSymbol (eglLib, "eglGetCurrentContext", eglGetCurrentContext)
        && loadSymbol (eglLib, "eglSwapBuffers", eglSwapBuffers)
        && loadSymbol (eglLib, "eglSwapInterval", eglSwapInterval)
        && loadSymbol (eglLib, "eglDestroySurface", eglDestroySurface)
        && loadSymbol (eglLib, "eglDestroyContext", eglDestroyContext)
        && loadSymbol (eglLib, "eglTerminate", eglTerminate)
        && loadSymbol (eglLib, "eglBindAPI", eglBindAPI)
        && loadSymbol (eglLib, "eglGetError", eglGetError);
        
        isInitialised = loadedWaylandEgl && loadedEgl;
        return isInitialised;
    }
    
    static inline wl_egl_window* (*wlWindowCreate) (wl_surface*, int, int) = nullptr;
    static inline void (*wlWindowDestroy) (wl_egl_window*) = nullptr;
    static inline void (*wlWindowResize) (wl_egl_window*, int, int, int, int) = nullptr;
    static inline void (*wlWindowGetAttachedSize) (wl_egl_window*, int*, int*) = nullptr;
    
    static inline EGLDisplay (*eglGetDisplay) (EGLNativeDisplayType) = nullptr;
    static inline EGLBoolean (*eglInitialize) (EGLDisplay, EGLint*, EGLint*) = nullptr;
    static inline EGLBoolean (*eglChooseConfig) (EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;
    static inline EGLSurface (*eglCreateWindowSurface) (EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;
    static inline EGLContext (*eglCreateContext) (EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;
    static inline EGLBoolean (*eglMakeCurrent) (EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
    static inline EGLContext (*eglGetCurrentContext) () = nullptr;
    static inline EGLBoolean (*eglSwapBuffers) (EGLDisplay, EGLSurface) = nullptr;
    static inline EGLBoolean (*eglSwapInterval) (EGLDisplay, EGLint) = nullptr;
    static inline EGLBoolean (*eglDestroySurface) (EGLDisplay, EGLSurface) = nullptr;
    static inline EGLBoolean (*eglDestroyContext) (EGLDisplay, EGLContext) = nullptr;
    static inline EGLBoolean (*eglTerminate) (EGLDisplay) = nullptr;
    static inline EGLBoolean (*eglBindAPI) (EGLenum) = nullptr;
    static inline EGLint (*eglGetError) () = nullptr;
    
    private:
    template <typename Func>
    static bool loadSymbol (juce::DynamicLibrary& lib, const char* name, Func& out)
    {
        out = reinterpret_cast<Func> (lib.getFunction (name));
        return out != nullptr;
    }
    
    static inline DynamicLibrary waylandEglLib = DynamicLibrary ("libwayland-egl.so.1");
    static inline DynamicLibrary eglLib = DynamicLibrary ("libEGL.so.1");
    static inline bool isInitialised = false;
};

class WaylandEGLDisplayManager
{
public:
    static WaylandEGLDisplayManager& getInstance()
    {
        static WaylandEGLDisplayManager instance;
        return instance;
    }
    
    EGLDisplay getDisplay()
    {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (eglDisplay == EGL_NO_DISPLAY)
        {
            auto* waylandDisplay = WaylandWindowSystem::getInstance()->getDisplay();
            eglDisplay = WaylandEGL::eglGetDisplay((EGLNativeDisplayType)waylandDisplay);
            
            if (eglDisplay != EGL_NO_DISPLAY)
            {
                EGLint major, minor;
                if (!WaylandEGL::eglInitialize(eglDisplay, &major, &minor))
                {
                    DBG("OpenGL WaylandNativeContext: Failed to initialize EGL.");
                    eglDisplay = EGL_NO_DISPLAY;
                }
            }
        }
        
        if (eglDisplay != EGL_NO_DISPLAY)
            refCount++;
            
        return eglDisplay;
    }
    
    void releaseDisplay()
    {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (--refCount == 0 && eglDisplay != EGL_NO_DISPLAY)
        {
            WaylandEGL::eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
        }
    }
    
private:
    WaylandEGLDisplayManager() = default;
    ~WaylandEGLDisplayManager()
    {
        if (eglDisplay != EGL_NO_DISPLAY)
        {
            WaylandEGL::eglTerminate(eglDisplay);
        }
    }
    
    std::mutex mutex;
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    int refCount = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaylandEGLDisplayManager)
};

//==============================================================================
class OpenGLContext::WaylandNativeContext : public OpenGLContext::NativeContext
{
    private:
    template <typename Traits>
    class ScopedEGLObject
    {
        public:
        using Type = typename Traits::Type;
        
        ScopedEGLObject() = default;
        
        explicit ScopedEGLObject (Type obj, EGLDisplay d)
        : object (obj), display (d) {}
        
        ScopedEGLObject (ScopedEGLObject&& other) noexcept
        : object (std::exchange (other.object, Type{})),
        display (std::exchange (other.display, EGL_NO_DISPLAY)) {}
        
        ScopedEGLObject& operator= (ScopedEGLObject&& other) noexcept
        {
            ScopedEGLObject { std::move (other) }.swap (*this);
            return *this;
        }
        
        ~ScopedEGLObject() noexcept
        {
            if (object != Type{} && display != EGL_NO_DISPLAY)
                Traits::destroy (display, object);
        }
        
        Type get() const { return object; }
        
        void reset() noexcept
        {
            *this = ScopedEGLObject();
        }
        
        void swap (ScopedEGLObject& other) noexcept
        {
            std::swap (other.object, object);
            std::swap (other.display, display);
        }
        
        bool operator== (const ScopedEGLObject& other) const
        {
            const auto tie = [] (const auto& x) { return std::tie (x.object, x.display); };
            return tie (*this) == tie (other);
        }
        
        bool operator!= (const ScopedEGLObject& other) const
        {
            return ! operator== (other);
        }
        
        private:
        Type object{};
        EGLDisplay display = EGL_NO_DISPLAY;
    };
    
    struct TraitsEGLContext
    {
        using Type = EGLContext;
        
        static void destroy (EGLDisplay display, Type context)
        {
            WaylandEGL::eglDestroyContext (display, context);
        }
    };
    
    struct TraitsEGLSurface
    {
        using Type = EGLSurface;
        
        static void destroy (EGLDisplay display, Type surface)
        {
            WaylandEGL::eglDestroySurface (display, surface);
        }
    };
    
    using PtrEGLContext = ScopedEGLObject<TraitsEGLContext>;
    using PtrEGLSurface = ScopedEGLObject<TraitsEGLSurface>;
    
    public:
    WaylandNativeContext (Component& comp,
                          const OpenGLPixelFormat& cPixelFormat,
                          void* shareContext,
                          bool useMultisamplingIn,
                          OpenGLVersion version)
    : component (comp),
    contextToShareWith (shareContext),
    versionRequired (version)
    {
        if (! WaylandEGL::initialise())
            return;
        
        eglDisplay = WaylandEGLDisplayManager::getInstance().getDisplay();
        if (eglDisplay == EGL_NO_DISPLAY)
        {
            DBG ("OpenGL WaylandNativeContext: Failed to get EGL display");
            return;
        }
        
        // Choose EGL configuration
        const std::vector<EGLint> optionalAttribs
        {
            EGL_SAMPLE_BUFFERS, cPixelFormat.multisamplingLevel,
            EGL_SAMPLES,        useMultisamplingIn
        };
        
        if (! tryChooseConfig (cPixelFormat, optionalAttribs) && ! tryChooseConfig (cPixelFormat, {}))
        {
            DBG ("OpenGL WaylandNativeContext: Failed to choose EGL config");
            return;
        }
        
        // Get window bounds
        auto windowBounds = component.getScreenBounds();
        
        // Ensure minimum size
        auto width = jmax (1, windowBounds.getWidth());
        auto height = jmax (1, windowBounds.getHeight());
        
        auto* peer = component.getPeer(); // TODO; passing in the parent peer might not be good?
        
        embeddedWindow = WaylandWindowSystem::getInstance()->createWindow (true, peer, true, WaylandWindowSystem::getInstance()->getWaylandWindowForPeer (peer));
        
        WaylandWindowSystem::getInstance()->setBounds (embeddedWindow, windowBounds);
        waylandSurface = WaylandWindowSystem::getInstance()->getSurfaceForWindow (embeddedWindow);
        
        WaylandWindowSystem::getInstance()->toFront (embeddedWindow, true);
      
        if (waylandSurface == nullptr)
        {
            DBG ("OpenGL WaylandNativeContext: Wayland surface is null");
            return;
        }
        
        auto* emptyRegion = WaylandSymbols::getInstance()->compositorCreateRegion (WaylandWindowSystem::getInstance()->getCompositor());
        WaylandSymbols::getInstance()->surfaceSetInputRegion (waylandSurface, emptyRegion);
        WaylandSymbols::getInstance()->regionDestroy (emptyRegion);

        // Now create the EGL window - surface should be properly configured
        waylandEglWindow = WaylandEGL::wlWindowCreate (waylandSurface, width, height);
        if (waylandEglWindow == nullptr)
        {
            DBG ("OpenGL WaylandNativeContext: Failed to create Wayland EGL window");
            return;
        }
    }
    
    ~WaylandNativeContext()
    {
        if (waylandEglWindow != nullptr)
        {
            WaylandEGL::wlWindowDestroy (waylandEglWindow);
            waylandEglWindow = nullptr;
        }
        
        if (eglDisplay != EGL_NO_DISPLAY)
        {
            WaylandEGLDisplayManager::getInstance().releaseDisplay();
            eglDisplay = EGL_NO_DISPLAY;
        }
        
        if (embeddedWindow)
            WaylandWindowSystem::getInstance()->destroyWindow (embeddedWindow);
    }
    
    InitResult initialiseOnRenderThread (OpenGLContext& c)
    {
        if (eglDisplay == EGL_NO_DISPLAY || bestConfig == nullptr || waylandEglWindow == nullptr)
            return InitResult::fatal;
        
        // Create EGL context
        const auto components = [&]() -> Optional<Version>
        {
            switch (versionRequired)
            {
                case OpenGLVersion::openGL3_2: return Version { 3, 2 };
                case OpenGLVersion::openGL4_1: return Version { 4, 1 };
                case OpenGLVersion::openGL4_3: return Version { 4, 3 };
                case OpenGLVersion::defaultGLVersion: break;
            }
            return {};
        }();
        
        std::vector<EGLint> contextAttribs;
        
        if (components.hasValue())
        {
            contextAttribs.insert (contextAttribs.end(), {
                EGL_CONTEXT_MAJOR_VERSION, components->major,
                EGL_CONTEXT_MINOR_VERSION, components->minor,
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
            });
            
#if JUCE_DEBUG
            contextAttribs.insert (contextAttribs.end(), {
                EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR
            });
#endif
        }
        
        contextAttribs.push_back (EGL_NONE);
        
        // Bind OpenGL API
        if (! WaylandEGL::eglBindAPI (EGL_OPENGL_API))
            return InitResult::fatal;
        
        renderContext = PtrEGLContext {
            WaylandEGL::eglCreateContext (eglDisplay, bestConfig, (EGLContext) contextToShareWith, contextAttribs.data()),
            eglDisplay
        };
        
        if (renderContext == PtrEGLContext{} || waylandEglWindow == nullptr)
            return InitResult::fatal;
        
        // Create EGL surface - this is where the crash might occur
        eglSurface = PtrEGLSurface {
            WaylandEGL::eglCreateWindowSurface (eglDisplay,
                                                bestConfig,
                                                (EGLNativeWindowType) waylandEglWindow,
                                                nullptr),
            eglDisplay
        };
        
        
        if (eglSurface == PtrEGLSurface{} || ! c.makeActive())
            return InitResult::fatal;
        
        context = &c;
        WaylandEGL::eglSwapInterval (eglDisplay, 0);
        
        return InitResult::success;
    }
    
    void shutdownOnRenderThread()
    {
        if (makeActive())
        {
            glClearColor (0.0f, 0.0f, 0.0f, 0.0f);  // or your background color
            glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            WaylandEGL::eglSwapBuffers (eglDisplay, eglSurface.get());
        }
        context = nullptr;
        deactivateCurrentContext();
        eglSurface.reset();
        renderContext.reset();
    }
    
    bool makeActive() const noexcept
    {
        return renderContext != PtrEGLContext{}
        && eglSurface != PtrEGLSurface{}
        && WaylandEGL::eglMakeCurrent (eglDisplay, eglSurface.get(), eglSurface.get(), renderContext.get());
    }
    
    bool isActive() const noexcept
    {
        return WaylandEGL::eglGetCurrentContext() == renderContext.get()
        && renderContext != PtrEGLContext{};
    }
    
    void deactivateCurrentContext()
    {
        if (auto display = WaylandEGL::eglGetDisplay (EGL_DEFAULT_DISPLAY))
        {
            WaylandEGL::eglMakeCurrent (display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
    }
  
    void swapBuffers()
    {
        if (eglSurface == PtrEGLSurface{} || waylandSurface == nullptr)
            return;
        WaylandEGL::eglSwapBuffers (eglDisplay, eglSurface.get());
    }
    
    void updateWindowPosition (Rectangle<int> newBounds)
    {
        bounds = newBounds;

        auto* parent = WaylandWindowSystem::getInstance()->getWaylandWindowForPeer (component.getPeer());
        int scale = jmax (1, WaylandWindowSystem::getInstance()->getScaleFactorForWindow (parent));
        int physicalWidth = bounds.getWidth() * scale;
        int physicalHeight = bounds.getHeight() * scale;

        if (waylandEglWindow != nullptr)
        {
            WaylandEGL::wlWindowResize (waylandEglWindow,
                                        jmax (scale, physicalWidth),
                                        jmax (scale, physicalHeight),
                                        0, 0);
        }
        WaylandWindowSystem::getInstance()->setBounds (embeddedWindow, bounds);

        if (bounds.isEmpty())
        {
            WaylandEGL::eglSwapBuffers (eglDisplay, eglSurface.get());
            WaylandEGL::eglSwapBuffers (eglDisplay, eglSurface.get());
            WaylandSymbols::getInstance()->surfaceCommit(waylandSurface);
        }
    }

    bool setSwapInterval (int numFramesPerSwap)
    {
        if (numFramesPerSwap == swapFrames)
            return true;
        
        if (eglDisplay != EGL_NO_DISPLAY)
        {
            swapFrames = numFramesPerSwap;
            return  WaylandEGL::eglSwapInterval (eglDisplay, numFramesPerSwap) == EGL_TRUE;
        }
        
        return false;
    }
    
    int getSwapInterval() const                 { return swapFrames; }
    bool createdOk() const noexcept             { return eglDisplay != EGL_NO_DISPLAY && bestConfig != nullptr; }
    void* getRawContext() const noexcept        { return renderContext.get(); }
    GLuint getFrameBufferID() const noexcept    { return 0; }
    
    void triggerRepaint()
    {
        //if (context != nullptr)
        //    context->triggerRepaint();
    }
    
    private:
    bool tryChooseConfig (const OpenGLPixelFormat& format, const std::vector<EGLint>& optionalAttribs)
    {
        std::vector<EGLint> allAttribs
        {
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE,        format.redBits,
            EGL_GREEN_SIZE,      format.greenBits,
            EGL_BLUE_SIZE,       format.blueBits,
            EGL_ALPHA_SIZE,      format.alphaBits,
            EGL_DEPTH_SIZE,      format.depthBufferBits,
            EGL_STENCIL_SIZE,    format.stencilBufferBits
        };
        
        allAttribs.insert (allAttribs.end(), optionalAttribs.begin(), optionalAttribs.end());
        allAttribs.push_back (EGL_NONE);
        
        EGLint numConfigs;
        EGLConfig configs[64];
        
        if (! WaylandEGL::eglChooseConfig (eglDisplay, allAttribs.data(), configs, 64, &numConfigs) || numConfigs == 0)
            return false;
        
        bestConfig = configs[0]; // Take the first matching configuration
        return true;
    }
    
    struct Version
    {
        int major, minor;
    };
    
    Component& component;
    PtrEGLContext renderContext;
    PtrEGLSurface eglSurface;
    
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLConfig bestConfig = nullptr;
    wl_surface* waylandSurface = nullptr;
    wl_egl_window* waylandEglWindow = nullptr;
    
    int swapFrames = 0;
    Rectangle<int> bounds;
    void* contextToShareWith;
    OpenGLVersion versionRequired;
    
    WaylandWindow* embeddedWindow = nullptr;
    
    OpenGLContext* context = nullptr;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaylandNativeContext)
};

//==============================================================================
bool OpenGLHelpers::isContextActive()
{
    if (WaylandWindowSystem::getInstance()->isWaylandAvailable() && WaylandEGL::eglGetCurrentContext)
    {
        return WaylandEGL::eglGetCurrentContext() != EGL_NO_CONTEXT;
    }
    
    XWindowSystemUtilities::ScopedXLock xLock;
    return glXGetCurrentContext() != nullptr;
}

} // namespace juce
