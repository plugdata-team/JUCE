/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
*/

namespace juce
{

//==============================================================================
class WaylandComponentPeer final : public ComponentPeer
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
        
        updateScaleFactorFromNewBounds (comp.getBounds());
        
        if(parentWindow)
        {
            windowH = WaylandWindowSystem::getInstance()->createWindow(true, this, parentWindow);
        }
        else {
            windowH = WaylandWindowSystem::getInstance()->createWindow((windowStyleFlags & windowIsTemporary), this);
        }
        
        setVisible(component.isVisible());
        setTitle (component.getName());
        getNativeRealtimeModifiers = []() -> ModifierKeys { return WaylandWindowSystem::getInstance()->getNativeRealtimeModifiers(); };

        updateVBlankTimer();
    }

    ~WaylandComponentPeer() override
    {
        // it's dangerous to delete a window on a thread other than the message thread.
        JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED
        repainter = nullptr;
        
        WaylandWindowSystem::getInstance()->destroyWindow(windowH);

        if (isAlwaysOnTop)
            --WindowUtilsInternal::numAlwaysOnTopPeers;
    }
    
    void onFrame()
    {
        onVBlank();
        vBlankManager.startTimerHz(60); // We only get wayland sync requests when we actually repaint, so use a timer from this point on to keep sending vblanks
    }

    void handleScreenSizeChange() override
    {
        if (!isFullScreen()) {
            //XWindowSystem::getInstance()->setFrameExtents(windowH, true);
        }
    }

    //==============================================================================
    void* getNativeHandle() const override
    {
        return (void*)WaylandWindowSystem::getInstance()->getSurfaceForWindow(windowH);
    }

    //==============================================================================
    void forceSetBounds (const Rectangle<int>& correctedNewBounds)
    {
        bounds = correctedNewBounds;
        updateScaleFactorFromNewBounds (bounds);

        WeakReference<Component> deletionChecker (&component);

        WaylandWindowSystem::getInstance()->setBounds(windowH, bounds);

        if (deletionChecker != nullptr)
        {
            updateBorderSize();
            handleMovedOrResized();
        }
    }

    void setBounds (const Rectangle<int>& newBounds, bool isNowFullScreen) override
    {
        const auto correctedNewBounds = newBounds.withSize (jmax (1, newBounds.getWidth()),
                                                            jmax (1, newBounds.getHeight()));

        if (bounds != correctedNewBounds)
            forceSetBounds (correctedNewBounds);
        
        //setFullScreen(isNowFullScreen);
    }

    Point<int> getScreenPosition (bool physical) const
    {
        return WaylandWindowSystem::getInstance()->getBounds(windowH).getPosition();
    }

    Rectangle<int> getBounds() const override
    {
        return bounds;
    }

    OptionalBorderSize getFrameSizeIfPresent() const override
    {
        return windowBorder;
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
        WaylandWindowSystem::getInstance()->setTitle(windowH, title);
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

        //return XWindowSystem::getInstance()->contains (windowH, localPos * currentScaleFactor);
        return WaylandWindowSystem::getInstance()->getBounds(windowH).contains(localPos);
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
        WaylandWindowSystem::getInstance()->grabFocus(windowH);
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
    void addOpenGLRepaintListener (Component* dummy)
    {
        if (dummy != nullptr)
            glRepaintListeners.addIfNotAlreadyThere (dummy);
    }

    void removeOpenGLRepaintListener (Component* dummy)
    {
        if (dummy != nullptr)
            glRepaintListeners.removeAllInstancesOf (dummy);
    }

    void repaintOpenGLContexts()
    {
        for (auto* c : glRepaintListeners)
            c->handleCommandMessage (0);
    }

    //==============================================================================
    WaylandWindow* getWindow()                               { return windowH; }
    WaylandWindow* getParentWindow()                         { return parentWindow; }
    void setParentWindow (WaylandWindow* newParent)          { parentWindow = newParent; }

    //==============================================================================
    bool isConstrainedNativeWindow() const
    {
        return constrainer != nullptr
            && (styleFlags & (windowHasTitleBar | windowIsResizable)) == (windowHasTitleBar | windowIsResizable)
            && ! isKioskMode();
    }

    // ============ debugging styleflags ===========
    juce::String get_style_flags(int styleFlag) const
    {
        std::map<int, juce::String> flag_descriptions = {
            {1 << 0, "windowAppearsOnTaskbar"},
            {1 << 1, "windowIsTemporary"},
            {1 << 2, "windowIgnoresMouseClicks"},
            {1 << 3, "windowHasTitleBar"},
            {1 << 4, "windowIsResizable"},
            {1 << 5, "windowHasMinimiseButton"},
            {1 << 6, "windowHasMaximiseButton"},
            {1 << 7, "windowHasCloseButton"},
            {1 << 8, "windowHasDropShadow"},
            {1 << 9, "windowRepaintedExplictly"},
            {1 << 10, "windowIgnoresKeyPresses"},
            {1 << 11, "windowRequiresSynchronousCoreGraphicsRendering"},
            {1 << 30, "windowIsSemiTransparent"}};

        juce::String description;

        for (const auto &flag_pair : flag_descriptions)
        {
            int flag_value = flag_pair.first;
            const juce::String &flag_name = flag_pair.second;

            if (styleFlag & flag_value)
            {
                if (!description.isEmpty())
                {
                    description += ", ";
                }
                description += flag_name;
            }
        }

        if (description.isEmpty())
        {
            return "No matching flags";
        }

        return description;
    }

    void updateWindowBounds()
    {
        if (windowH == 0)
        {
            jassertfalse;
            return;
        }

        //if (isConstrainedNativeWindow())
        //    XWindowSystem::getInstance()->updateConstraints (windowH);

        bounds = WaylandWindowSystem::getInstance()->getBounds (windowH);

        updateScaleFactorFromNewBounds (bounds);

        updateVBlankTimer();
    }

    void updateBorderSize()
    {
        windowBorder = ComponentPeer::OptionalBorderSize { BorderSize<int>() };
    }

    bool setWindowAssociation (::Window windowIn)
    {
        clearWindowAssociation();
        association = { this, windowIn };
        return association.isValid();
    }

    void clearWindowAssociation() { association = {}; }

    void startHostManagedResize (Point<int>, ResizableBorderComponent::Zone zone) override
    {
        WaylandWindowSystem::getInstance()->startHostManagedResize (windowH, zone);
    }

    //==============================================================================
    static bool isActiveApplication;
    bool focused = false;
    bool inConfigureNotifyHandler = false;
    WaylandWindow* windowH = nullptr;
    
private:
    //==============================================================================
    class WaylandRepaintManager
    {
    public:
        WaylandRepaintManager (WaylandComponentPeer& p)
            : peer (p),
              isSemiTransparentWindow ((peer.getStyleFlags() & ComponentPeer::windowIsSemiTransparent) != 0)
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
            WaylandWindowSystem::getInstance()->requestFrame(peer.windowH);
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
                    
                    image = Image(Image::ARGB, totalArea.getWidth(), totalArea.getHeight(), true);
                    
                    if (wasImageNull)
                    {
                        // TODO: does wayland need this?
                        
                        // After calling createImage() XWindowSystem::getWindowBounds() will return
                        // changed coordinates that look like the result of some position
                        // defaulting mechanism. If we handle a configureNotifyEvent after
                        // createImage() and before we would issue new, valid coordinates, we will
                        // apply these default, unwanted coordinates to our window. To avoid that
                        // we immediately send another positioning message to guarantee that the
                        // next configureNotifyEvent will read valid values.
                        //
                        // This issue only occurs right after peer creation, when the image is
                        // null. Updating when only the width or height is changed would lead to
                        // incorrect behaviour.
                        peer.forceSetBounds (detail::ScalingHelpers::scaledScreenPosToUnscaled (peer.component, peer.component.getBoundsInParent()));
                    }
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
                   WaylandWindowSystem::getInstance()->blitToWindow(peer.windowH, image, i, totalArea);
                 
                WaylandWindowSystem::getInstance()->commit(peer.windowH);
            }

            lastTimeImageUsed = Time::getApproximateMillisecondCounter();
        }

    private:
        WaylandComponentPeer& peer;
        const bool isSemiTransparentWindow;
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
        return relativePosition + t.getScreenPosition (false).toFloat();
    }

    template <typename This>
    static Point<float> globalToLocal (This& t, Point<float> screenPosition)
    {
        return screenPosition - t.getScreenPosition (false).toFloat();
    }


    void updateScaleFactorFromNewBounds (const Rectangle<int>& newBounds)
    {
        Point<int> translation = (parentWindow != 0 ? getScreenPosition (false) : Point<int>());
        const auto& desktop = Desktop::getInstance();

        if (auto* display = desktop.getDisplays().getDisplayForRect (newBounds.translated (translation.x, translation.y), false))
        {
            auto newScaleFactor = display->scale / desktop.getGlobalScaleFactor();

            if (! approximatelyEqual (newScaleFactor, currentScaleFactor))
            {
                currentScaleFactor = newScaleFactor;
                if(windowH) {
                    auto* wlSurface = WaylandWindowSystem::getInstance()->getSurfaceForWindow(windowH);
                    wl_surface_set_buffer_scale(wlSurface, roundToInt(currentScaleFactor));
                }
                scaleFactorListeners.call ([&] (ScaleFactorListener& l) { l.nativeScaleFactorChanged (currentScaleFactor); });
            }
        }
    }

    void onVBlank()
    {
        vBlankListeners.call ([] (auto& l) { l.onVBlank(); });

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

            if (vBlankManager.getTimerInterval() != frequencyToUse)
                vBlankManager.startTimerHz (frequencyToUse);
        }
        else {
            vBlankManager.startTimerHz (60);
        }
    }


    //==============================================================================
    std::unique_ptr<WaylandRepaintManager> repainter;
    TimedCallback vBlankManager { [this]() { onVBlank(); } };

    WaylandWindow* parentWindow = nullptr;
    Rectangle<int> bounds;
    ComponentPeer::OptionalBorderSize windowBorder;
    bool isAlwaysOnTop = false;
    double currentScaleFactor = 1.0;
    Array<Component*> glRepaintListeners;
    ScopedWindowAssociation association;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaylandComponentPeer)
};

bool WaylandComponentPeer::isActiveApplication = false;

} // namespace juce



