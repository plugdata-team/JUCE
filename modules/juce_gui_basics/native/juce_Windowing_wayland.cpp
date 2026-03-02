/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
 */

namespace juce
{

//==============================================================================
class WaylandComponentPeer final : public ComponentPeer, public Timer
{
    public:
    WaylandComponentPeer (Component& comp, int windowStyleFlags, WaylandWindow* parentToAddTo)
    : ComponentPeer (comp, windowStyleFlags),
    isAlwaysOnTop (comp.isAlwaysOnTop())
    {
        // it's dangerous to create a window on a thread other than the message thread.
        JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED
        
        if (isAlwaysOnTop)
            ++WindowUtilsInternal::numAlwaysOnTopPeers;
        
        repainter = std::make_unique<WaylandRepaintManager> (*this);
        
        parentWindow = parentToAddTo;
        
        if (parentWindow)
        {
            windowH = WaylandWindowSystem::getInstance()->createWindow (true, this, isAlwaysOnTop, parentWindow);
        }
        else
        {
            bool isSubsurface = (windowStyleFlags & windowIsTemporary) && ! (windowStyleFlags & windowHasTitleBar);
            windowH = WaylandWindowSystem::getInstance()->createWindow (isSubsurface, this, isAlwaysOnTop);
        }
        
        updateScaleFactor();
        
        setVisible (component.isVisible());
        setTitle (component.getName());
        getNativeRealtimeModifiers = []() -> ModifierKeys
        {
            return WaylandWindowSystem::getInstance()->getNativeRealtimeModifiers();
        };
        
        updateVBlankTimer();
    }
    
    ~WaylandComponentPeer() override
    {
        // it's dangerous to delete a window on a thread other than the message thread.
        JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED
        repainter = nullptr;
        
        WaylandWindowSystem::getInstance()->destroyWindow (windowH);
        
        if (isAlwaysOnTop)
            --WindowUtilsInternal::numAlwaysOnTopPeers;
        
    }
    
    bool isShowing() const override
    {
        return true;
    }
    
    void onFrame()
    {
        timerCallback();
        // We only get wayland sync requests when we actually repaint, so use a timer from this point on to keep sending vblanks
        startTimer (getTimerInterval());
    }
    
    void handleScreenSizeChange() override
    {
    }
    
    //==============================================================================
    void* getNativeHandle() const override
    {
        return (void*)WaylandWindowSystem::getInstance()->getSurfaceForWindow (windowH);
    }
    
    void setBounds (const Rectangle<int>& newBounds, bool isNowFullScreen) override
    {
        const auto correctedNewBounds = newBounds.withSize (jmax (1, newBounds.getWidth()),
                                                            jmax (1, newBounds.getHeight()));
        updateScaleFactor();
        bounds = correctedNewBounds;
        
        WeakReference<Component> deletionChecker (&component);
        
        WaylandWindowSystem::getInstance()->setBounds (windowH, correctedNewBounds);
        
        if (deletionChecker != nullptr)
        {
            handleMovedOrResized();
        }
        if(isNowFullScreen != fullscreen)
        {
            getComponent().resized();
            fullscreen = isNowFullScreen;
        }
    }
    
    Point<int> getScreenPosition() const
    {
        return WaylandWindowSystem::getInstance()->getBounds (windowH).getPosition();
    }
    
    Rectangle<int> getBounds() const override
    {
        return bounds;
    }
    
    OptionalBorderSize getFrameSizeIfPresent() const override
    {
        return {};
    }
    
    BorderSize<int> getFrameSize() const override
    {
        const auto optionalBorderSize = getFrameSizeIfPresent();
        return optionalBorderSize ? (*optionalBorderSize) : BorderSize<int>();
    }
    
    Point<float> localToGlobal (Point<float> relativePosition) override
    {
        return localToGlobal (*this, relativePosition);
    }
    
    Point<float> globalToLocal (Point<float> screenPosition) override
    {
        return globalToLocal (*this, screenPosition);
    }
    
    using ComponentPeer::localToGlobal;
    using ComponentPeer::globalToLocal;
    
    //==============================================================================
    StringArray getAvailableRenderingEngines() override
    {
        return { "Software Renderer" };
    }
    
    void setVisible (bool shouldBeVisible) override
    {
        WaylandWindowSystem::getInstance()->setVisible (windowH, shouldBeVisible);
    }
    
    void setTitle (const String& title) override
    {
        WaylandWindowSystem::getInstance()->setTitle (windowH, title);
    }
    
    void setMinimised (bool shouldBeMinimised) override
    {
        WaylandWindowSystem::getInstance()->setMinimised (windowH, shouldBeMinimised);
    }
    
    bool isMinimised() const override
    {
        return WaylandWindowSystem::getInstance()->isMinimised (windowH);
    }
    
    void setFullScreen (bool shouldBeFullScreen) override
    {
        WaylandWindowSystem::getInstance()->setFullscreen (windowH, shouldBeFullScreen);
    }
    
    bool isFullScreen() const override
    {
        return WaylandWindowSystem::getInstance()->isFullscreen (windowH);
    }
    
    bool contains (Point<int> localPos, bool trueIfInAChildWindow) const override
    {
        if (! bounds.withZeroOrigin().contains (localPos))
            return false;
        
        for (int i = Desktop::getInstance().getNumComponents(); --i >= 0;)
        {
            auto* c = Desktop::getInstance().getComponent (i);
            
            if (c == &component)
                break;
            
            if (! c->isVisible())
                continue;
            
            auto* otherPeer = c->getPeer();
            jassert (otherPeer == nullptr || dynamic_cast<WaylandComponentPeer*> (c->getPeer()) != nullptr);
            
            if (auto* peer = static_cast<WaylandComponentPeer*> (otherPeer))
                if (peer->contains (globalToLocal (*peer, localToGlobal (*this, localPos.toFloat())).roundToInt(), true))
                    return false;
        }
        
        if (trueIfInAChildWindow)
            return true;
        
        return (WaylandWindowSystem::getInstance()->getBounds (windowH)).contains (localPos);
    }
    
    void toFront (bool makeActive) override
    {
        if (makeActive)
        {
            setVisible (true);
            grabFocus();
        }
        
        WaylandWindowSystem::getInstance()->toFront (windowH, true);
        handleBroughtToFront();
    }
    
    void toBehind (ComponentPeer* other) override
    {
        if (auto* otherPeer = dynamic_cast<WaylandComponentPeer*> (other))
        {
            setMinimised (false);
            WaylandWindowSystem::getInstance()->toBehind (windowH, otherPeer->windowH);
        }
        else
        {
            jassertfalse; // wrong type of window?
        }
    }
    
    bool isFocused() const override
    {
        return WaylandWindowSystem::getInstance()->isFocused (windowH);
    }
    
    void grabFocus() override
    {
        WaylandWindowSystem::getInstance()->grabFocus (windowH);
        isActiveApplication = true;
    }
    
    //==============================================================================
    void repaint (const Rectangle<int>& area) override
    {
        if (repainter != nullptr)
            repainter->repaint (area.getIntersection (bounds.withZeroOrigin()));
    }
    
    void performAnyPendingRepaintsNow() override
    {
        if (repainter != nullptr)
            repainter->performAnyPendingRepaintsNow();
    }
    
    void setIcon (const Image& newIcon) override
    {
        ignoreUnused (newIcon);
        jassertfalse;
        // Not supported, wayland sets the icon from a .desktop file instead
    }
    
    double getPlatformScaleFactor() const noexcept override
    {
        return currentScaleFactor;
    }
    
    void setAlpha (float) override                                  {}
    bool setAlwaysOnTop (bool) override                             { return false; }
    void textInputRequired (Point<int>, TextInputTarget&) override  {}
    
    //==============================================================================
    bool isConstrainedNativeWindow() const
    {
        return constrainer != nullptr
        && (styleFlags & (windowHasTitleBar | windowIsResizable)) == (windowHasTitleBar | windowIsResizable)
        && ! isKioskMode();
    }
    
    void updateWindowBounds()
    {
        if (windowH == 0)
        {
            jassertfalse;
            return;
        }
        
        if (isConstrainedNativeWindow())
            WaylandWindowSystem::getInstance()->updateConstraints (windowH);
        
        bounds = WaylandWindowSystem::getInstance()->getBounds (windowH);
        
        updateScaleFactor();
        
        updateVBlankTimer();
    }
    
    void startHostManagedResize (Point<int>, ResizableBorderComponent::Zone zone) override
    {
        WaylandWindowSystem::getInstance()->startHostManagedResize (windowH, zone);
    }
        
    void updateScaleFactor()
    {
        auto windowScale = (double)WaylandWindowSystem::getInstance()->getScaleFactorForWindow (windowH);
        if (! approximatelyEqual (windowScale, currentScaleFactor))
        {
            currentScaleFactor = windowScale;
            repaint (bounds.withZeroOrigin());
            scaleFactorListeners.call ([&] (ScaleFactorListener& l) { l.nativeScaleFactorChanged (currentScaleFactor); });
        }
    }
    
    
    WaylandWindow* getWindow() { return windowH; }

    static bool isActiveApplication;
    
    private:
    //==============================================================================
    class WaylandRepaintManager
    {
        public:
        WaylandRepaintManager (WaylandComponentPeer& p)
        : peer (p)
        {
        }
        
        void dispatchDeferredRepaints()
        {
            if (! regionsNeedingRepaint.isEmpty())
                performAnyPendingRepaintsNow();
            else if (Time::getApproximateMillisecondCounter() > lastTimeImageUsed + 3000)
                image = Image();
        }
        
        void repaint (Rectangle<int> area)
        {
            WaylandWindowSystem::getInstance()->requestFrame (peer.windowH);
            regionsNeedingRepaint.add (area * peer.currentScaleFactor);
        }
        
        void performAnyPendingRepaintsNow()
        {
            auto originalRepaintRegion = regionsNeedingRepaint;
            regionsNeedingRepaint.clear();
            auto totalArea = originalRepaintRegion.getBounds();
            
            if (! totalArea.isEmpty())
            {
                const auto wasImageNull = image.isNull();
                
                if (wasImageNull || image.getWidth() < totalArea.getWidth()
                    || image.getHeight() < totalArea.getHeight())
                {
                    image = Image (Image::ARGB, totalArea.getWidth(), totalArea.getHeight(), true);
                }
                
                
                RectangleList<int> adjustedList (originalRepaintRegion);
                adjustedList.offsetAll (-totalArea.getX(), -totalArea.getY());
                
                for (auto& i : originalRepaintRegion)
                    image.clear (i - totalArea.getPosition());
                
                {
                    auto context = peer.getComponent().getLookAndFeel()
                        .createGraphicsContext (image, -totalArea.getPosition(), adjustedList);
                    
                    context->addTransform (AffineTransform::scale ((float) peer.currentScaleFactor));
                    peer.handlePaint (*context);
                }
                
                for (auto& i : originalRepaintRegion)
                    WaylandWindowSystem::getInstance()->blitToWindow (peer.windowH, image, i, totalArea);
                
                WaylandWindowSystem::getInstance()->commit (peer.windowH);
            }
            
            lastTimeImageUsed = Time::getApproximateMillisecondCounter();
        }
        
        private:
        WaylandComponentPeer& peer;
        Image image;
        uint32 lastTimeImageUsed = 0;
        RectangleList<int> regionsNeedingRepaint;
        
        bool useARGBImagesForRendering = true;
        JUCE_DECLARE_NON_COPYABLE (WaylandRepaintManager)
    };
    
    //==============================================================================
    template <typename This>
    static Point<float> localToGlobal (This& t, Point<float> relativePosition)
    {
        return relativePosition + t.getScreenPosition().toFloat();
    }
    
    template <typename This>
    static Point<float> globalToLocal (This& t, Point<float> screenPosition)
    {
        return screenPosition - t.getScreenPosition().toFloat();
    }

    void timerCallback() override
    {
        const auto timestampSec = Time::getMillisecondCounterHiRes() / 1000.0;
        callVBlankListeners (timestampSec);

        if (repainter != nullptr)
            repainter->dispatchDeferredRepaints();
    }
    
    void updateVBlankTimer()
    {
        if (auto* display = Desktop::getInstance().getDisplays().getDisplayForRect (bounds))
        {
            // Some systems fail to set an explicit refresh rate, or ask for a refresh rate of 0
            // (observed on Raspbian Bullseye over VNC). In these situations, use a fallback value.
            const auto newIntFrequencyHz = roundToInt (display->verticalFrequencyHz.value_or (0.0));
            const auto frequencyToUse = newIntFrequencyHz != 0 ? newIntFrequencyHz : 100;
            
            if (getTimerInterval() != frequencyToUse)
                startTimerHz (frequencyToUse);
        }
        else {
            startTimerHz (60);
        }
    }
    
    //==============================================================================
    WaylandWindow* windowH = nullptr;
    WaylandWindow* parentWindow = nullptr;
    std::unique_ptr<WaylandRepaintManager> repainter;
    Rectangle<int> bounds;
    bool isAlwaysOnTop = false;
    bool fullscreen = false;
    double currentScaleFactor = 1.0;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaylandComponentPeer)
};

bool WaylandComponentPeer::isActiveApplication = false;

} // namespace juce

