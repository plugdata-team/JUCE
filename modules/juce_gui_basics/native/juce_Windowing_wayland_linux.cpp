/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-9-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

class WaylandComponentPeer;

namespace
{
    struct WaylandPeerCallbackState
    {
        WeakReference<WaylandComponentPeer> peer;
    };

    WeakReference<WaylandComponentPeer> getPeerFromCallbackState (void* data)
    {
        if (auto* state = static_cast<WaylandPeerCallbackState*> (data))
            return state->peer;

        return {};
    }
}

//==============================================================================
// Scale state for a surface and the buffer geometry derived from it.
// wp_fractional_scale_v1 expresses scales in 120ths of a unit, so the arithmetic
// stays in whole 120ths until the final rounding to avoid floating point drift.
class WaylandSurfaceScale final
{
public:
    enum class BufferMappingMethod
    {
        integerBufferScale,
        viewport
    };

    struct BufferGeometry
    {
        Rectangle<int> bufferBounds;
        int bufferScale = 1;
        std::optional<Rectangle<int>> viewportDestination;
    };

    struct Update
    {
        std::optional<double> scaleFactorToReport;
        bool bufferGeometryChanged = false;
    };

    Update setPreferredFractionalScale120 (int scale120)
    {
        return updateScaleState ([&]
        {
            // Buffer memory grows with the square of the scale
            if (1 <= scale120 && scale120 <= maximumScale120)
                preferredFractionalScale120 = scale120;
        });
    }

    Update setPreferredBufferScale (int scale)
    {
        return updateScaleState ([&]
        {
            // Buffer memory grows with the square of the scale
            if (1 <= scale && scale <= maximumScale120 / scaleDenominator)
                preferredBufferScale = scale;
        });
    }

    Update setOutputScale (int scale)
    {
        return updateScaleState ([&]
        {
            outputScale = jlimit (1, maximumScale120 / scaleDenominator, scale);
        });
    }

    Update setOverrideScale (std::optional<double> scale)
    {
        return updateScaleState ([&]
        {
            // A non-positive scale cannot be rendered, so it clears the override.
            if (scale.has_value() && *scale > 0.0)
                overrideScale = *scale;
            else
                overrideScale = {};
        });
    }

    std::optional<double> getOverrideScale() const  { return overrideScale; }

    double getScaleFactor() const
    {
        return getRenderScaleValue().factor;
    }

    int getIntegerCompositorScale() const
    {
        return preferredBufferScale.value_or (outputScale);
    }

    double getLogicalToSurfaceScale (BufferMappingMethod method) const
    {
        const auto compositorScale = method == BufferMappingMethod::viewport
                                   ? getCompositorScaleValue().factor
                                   : (double) getIntegerCompositorScale();
        return getRenderScaleValue().factor / compositorScale;
    }

    BufferGeometry getBufferGeometry (Rectangle<int> logicalSize, BufferMappingMethod method) const
    {
        BufferGeometry result;

        if (method == BufferMappingMethod::viewport)
        {
            // set_buffer_scale stays at 1 because the viewport defines the mapping
            // from the whole-pixel buffer to surface coordinates.
            const auto renderScale = getRenderScaleValue();
            result.bufferBounds = getScaledBounds (logicalSize, renderScale);

            if (overrideScale.has_value())
            {
                const auto compositorScale = getCompositorScaleValue().factor;
                const ScaleValue surfaceScale { renderScale.factor / compositorScale, {} };
                result.viewportDestination = getScaledBounds (logicalSize, surfaceScale);
            }
            else
            {
                result.viewportDestination = logicalSize.withZeroOrigin();
            }

            return result;
        }

        // Without a viewport, buffer dimensions must be multiples of the compositor's
        // integer scale. Round the surface size before scaling it.
        const auto compositorScale = getIntegerCompositorScale();
        const ScaleValue surfaceScale { overrideScale.value_or ((double) compositorScale) / compositorScale, {} };
        result.bufferBounds = getScaledBounds (logicalSize, surfaceScale) * compositorScale;
        result.bufferScale = compositorScale;
        return result;
    }

    int convertSurfaceExtentToLogical (int surfaceExtent, BufferMappingMethod method) const
    {
        if (surfaceExtent == 0)
            return 0;

        // Configure sizes describe new geometry, so invert the ideal scale before
        // rounded buffer geometry exists.
        const auto compositorScale = method == BufferMappingMethod::viewport
                                   ? getCompositorScaleValue().factor
                                   : (double) getIntegerCompositorScale();
        return jmax (1, (int) std::round (surfaceExtent * compositorScale / getRenderScaleValue().factor));
    }

    // The size the surface occupies in surface coordinates, which is the buffer mapped through
    // either the viewport destination or the buffer scale.
    Point<double> getSurfaceSize (Rectangle<int> logicalSize, BufferMappingMethod method) const
    {
        const auto geometry = getBufferGeometry (logicalSize, method);
        const auto surfaceBounds = geometry.viewportDestination.value_or (geometry.bufferBounds);
        const auto divisor = geometry.viewportDestination.has_value() ? 1.0 : (double) geometry.bufferScale;

        return { surfaceBounds.getWidth() / divisor, surfaceBounds.getHeight() / divisor };
    }

    Point<float> convertSurfacePointToLogical (Point<float> point,
                                               Rectangle<int> logicalSize,
                                               BufferMappingMethod method) const
    {
        // Input follows the rounded geometry used by the committed buffer.
        const auto surfaceSize = getSurfaceSize (logicalSize, method);

        if (surfaceSize.x <= 0.0 || surfaceSize.y <= 0.0)
            return point;

        return Point<double> { point.x * (logicalSize.getWidth() / surfaceSize.x),
                               point.y * (logicalSize.getHeight() / surfaceSize.y) }.toFloat();
    }

    // Rounding the buffer size can make its ratio to the logical size differ from the scale factor.
    // Round start coordinates down and end coordinates up so the result covers the entire scaled rectangle.
    static Rectangle<int> mapLogicalRectToBuffer (Rectangle<int> logicalRect,
                                                  Rectangle<int> logicalBounds,
                                                  Rectangle<int> bufferBounds)
    {
        [[maybe_unused]] const auto isNonNegative = [] (Rectangle<int> rect)
        {
            return rect.getX() >= 0 && rect.getY() >= 0 && rect.getWidth() >= 0 && rect.getHeight() >= 0;
        };

        // Callers must pass regions clipped to the component bounds
        jassert (isNonNegative (logicalRect) && isNonNegative (logicalBounds) && isNonNegative (bufferBounds));

        if (logicalBounds.isEmpty())
            return {};

        const auto mapStart = [] (int position, int bufferExtent, int logicalExtent)
        {
            return (int) (((int64) position * bufferExtent) / logicalExtent);
        };

        const auto mapEnd = [] (int position, int bufferExtent, int logicalExtent)
        {
            return (int) (((int64) position * bufferExtent + logicalExtent - 1) / logicalExtent);
        };

        const auto left = mapStart (logicalRect.getX(), bufferBounds.getWidth(), logicalBounds.getWidth());
        const auto top = mapStart (logicalRect.getY(), bufferBounds.getHeight(), logicalBounds.getHeight());
        const auto right = mapEnd (logicalRect.getRight(), bufferBounds.getWidth(), logicalBounds.getWidth());
        const auto bottom = mapEnd (logicalRect.getBottom(), bufferBounds.getHeight(), logicalBounds.getHeight());

        return { left, top, right - left, bottom - top };
    }

    static Rectangle<int> mapLogicalRectToSurface (Rectangle<int> logicalRect,
                                                   Rectangle<int> logicalBounds,
                                                   Rectangle<int> surfaceBounds)
    {
        if (logicalBounds.isEmpty())
            return {};

        // Round each shared logical boundary once so adjacent child surfaces meet without gaps or overlaps.
        const auto mapPosition = [] (int position, int logicalStart, int logicalExtent,
                                     int surfaceStart, int surfaceExtent)
        {
            return surfaceStart + roundToInt ((double) (position - logicalStart)
                                              * surfaceExtent / logicalExtent);
        };

        const auto left = mapPosition (logicalRect.getX(), logicalBounds.getX(), logicalBounds.getWidth(),
                                       surfaceBounds.getX(), surfaceBounds.getWidth());
        const auto top = mapPosition (logicalRect.getY(), logicalBounds.getY(), logicalBounds.getHeight(),
                                      surfaceBounds.getY(), surfaceBounds.getHeight());
        const auto right = mapPosition (logicalRect.getRight(), logicalBounds.getX(), logicalBounds.getWidth(),
                                        surfaceBounds.getX(), surfaceBounds.getWidth());
        const auto bottom = mapPosition (logicalRect.getBottom(), logicalBounds.getY(), logicalBounds.getHeight(),
                                         surfaceBounds.getY(), surfaceBounds.getHeight());

        return { left, top, right - left, bottom - top };
    }

private:
    struct ScaleValue
    {
        double factor = 1.0;
        std::optional<int> exactScale120;
    };

    // preferred_scale values are 120ths of a scale unit.
    static constexpr int scaleDenominator = 120;

    // Values above 16x are not plausible for a display.
    static constexpr int maximumScale120 = scaleDenominator * 16;

    template <typename Callback>
    Update updateScaleState (Callback&& callback)
    {
        const auto previousRenderScale = getScaleFactor();
        const auto previousCompositorScale = getCompositorScaleValue().factor;
        callback();
        const auto nextRenderScale = getScaleFactor();
        const auto nextCompositorScale = getCompositorScaleValue().factor;
        const auto renderScaleChanged = ! approximatelyEqual (previousRenderScale, nextRenderScale);

        Update result;
        result.bufferGeometryChanged = renderScaleChanged
                                    || ! approximatelyEqual (previousCompositorScale, nextCompositorScale);

        if (renderScaleChanged)
            result.scaleFactorToReport = nextRenderScale;

        return result;
    }

    ScaleValue getRenderScaleValue() const
    {
        if (overrideScale.has_value())
            return { *overrideScale, {} };

        return getCompositorScaleValue();
    }

    ScaleValue getCompositorScaleValue() const
    {
        // The fractional protocol gives a more precise preference.
        const auto scale120 = preferredFractionalScale120.value_or (getIntegerCompositorScale()
                                                                    * scaleDenominator);
        return { scale120 / (double) scaleDenominator, scale120 };
    }

    static Rectangle<int> getScaledBounds (Rectangle<int> logicalSize, const ScaleValue& scale)
    {
        return { scaledExtent (logicalSize.getWidth(), scale),
                 scaledExtent (logicalSize.getHeight(), scale) };
    }

    // fractional-scale-v1 specifies rounding to the nearest whole pixel, with halfway
    // values rounded away from zero. Use the same rule for all derived scale geometry.
    static int scaledExtent (int logicalExtent, const ScaleValue& scale)
    {
        if (logicalExtent == 0)
            return 0;

        if (! scale.exactScale120.has_value())
            return jmax (1, (int) std::round (logicalExtent * scale.factor));

        return jmax (1, (logicalExtent * *scale.exactScale120 + scaleDenominator / 2) / scaleDenominator);
    }

    std::optional<int> preferredFractionalScale120;
    std::optional<int> preferredBufferScale;
    std::optional<double> overrideScale;
    int outputScale = 1;
};

//==============================================================================
class WaylandSurfaceOutputs final
{
public:
    WaylandSurfaceOutputs() = default;

    void add (wl_output* output)
    {
        if (output != nullptr && ! contains (output))
            outputs.push_back (output);
    }

    void remove (wl_output* output)
    {
        if (const auto it = std::find (outputs.begin(), outputs.end(), output); it != outputs.end())
            outputs.erase (it);
    }

    bool contains (wl_output* output) const
    {
        return std::find (outputs.begin(), outputs.end(), output) != outputs.end();
    }

    int size() const noexcept   { return (int) outputs.size(); }

    template <typename ScaleForOutput>
    int getLargestScale (int fallbackScale, ScaleForOutput&& scaleForOutput) const
    {
        std::optional<int> largestScale;

        for (auto* output : outputs)
            if (const auto scale = scaleForOutput (output))
                largestScale = jmax (largestScale.value_or (*scale), *scale);

        return largestScale.value_or (fallbackScale);
    }

private:
    std::vector<wl_output*> outputs;

    JUCE_DECLARE_NON_COPYABLE (WaylandSurfaceOutputs)
};

//==============================================================================
static RectangleList<int> computeRegionsToPaint (const RectangleList<int>& dirtyRegions,
                                                 const std::optional<RectangleList<int>>& knownStaleRegions,
                                                 bool scratchImageWasRecreated,
                                                 Rectangle<int> logicalBounds)
{
    if (! knownStaleRegions.has_value() || scratchImageWasRecreated)
        return logicalBounds;

    auto result = dirtyRegions;
    result.add (*knownStaleRegions);
    return result;
}

static RectangleList<int> computeRegionsToReportAsDamage (const RectangleList<int>& dirtyBufferRegions,
                                                          const std::optional<RectangleList<int>>& knownStaleRegions,
                                                          Rectangle<int> bufferBounds)
{
    if (! knownStaleRegions.has_value())
        return bufferBounds;

    // Discard repaint requests that do not overlap the current window before calling this function.
    jassert (! dirtyBufferRegions.isEmpty());

    if (dirtyBufferRegions.isEmpty())
        return bufferBounds;

    return dirtyBufferRegions;
}

static WaylandSizeConstraints getWaylandSizeConstraints (const ComponentBoundsConstrainer& constrainer)
{
    if (const auto* bordered = dynamic_cast<const BorderedComponentBoundsConstrainer*> (&constrainer))
    {
        if (const auto* wrapped = bordered->getWrappedConstrainer())
        {
            const auto constraints = getWaylandSizeConstraints (*wrapped);
            const auto border = bordered->getAdditionalBorder();
            const Point<int> borderSize { border.getLeftAndRight(), border.getTopAndBottom() };

            const auto addBorder = [borderSize] (Point<int> size)
            {
                const auto addExtent = [] (int extent, int borderExtent)
                {
                    return (int) jmin ((int64) std::numeric_limits<int>::max(),
                                       (int64) extent + borderExtent);
                };

                return Point<int> { addExtent (size.x, borderSize.x),
                                    addExtent (size.y, borderSize.y) };
            };

            return { addBorder (constraints.minimum), addBorder (constraints.maximum) };
        }
    }

    return { { constrainer.getMinimumWidth(), constrainer.getMinimumHeight() },
             { constrainer.getMaximumWidth(), constrainer.getMaximumHeight() } };
}

static uint32_t getXdgResizeEdgeForZone (ResizableBorderComponent::Zone zone)
{
    using F = ResizableBorderComponent::Zone::Zones;

    switch (zone.getZoneFlags())
    {
        case F::top | F::left:      return WaylandProtocol::xdgToplevelResizeEdgeTopLeft;
        case F::top:                return WaylandProtocol::xdgToplevelResizeEdgeTop;
        case F::top | F::right:     return WaylandProtocol::xdgToplevelResizeEdgeTopRight;
        case F::right:              return WaylandProtocol::xdgToplevelResizeEdgeRight;
        case F::bottom | F::right:  return WaylandProtocol::xdgToplevelResizeEdgeBottomRight;
        case F::bottom:             return WaylandProtocol::xdgToplevelResizeEdgeBottom;
        case F::bottom | F::left:   return WaylandProtocol::xdgToplevelResizeEdgeBottomLeft;
        case F::left:               return WaylandProtocol::xdgToplevelResizeEdgeLeft;
    }

    return WaylandProtocol::xdgToplevelResizeEdgeNone;
}

//==============================================================================
class WaylandComponentPeer final : public ComponentPeer,
                                   private WaylandToplevel::Delegate,
                                   private WaylandPopup::Delegate,
                                   private WaylandInputHandlerListener,
                                   private WaylandDataDeviceListener,
                                   private WaylandOutputListener
{
public:
    WaylandComponentPeer (Component& comp, int windowStyleFlags)
        : ComponentPeer (comp, windowStyleFlags),
          logicalBounds (comp.getBoundsInParent())
    {
        JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED

        auto* windowSystem = WaylandWindowSystem::getInstance();

        if (! windowSystem->isWaylandAvailable())
            return;

        surface.reset (WaylandProtocol::wlCompositorCreateSurface (windowSystem->getCompositor()));

        if (surface == nullptr)
            return;

        callbackState = std::make_unique<WaylandPeerCallbackState>();
        callbackState->peer = this;

        if ((windowStyleFlags & windowIgnoresMouseClicks) != 0)
        {
            // An empty input region lets pointer events reach the surface beneath.
            auto* compositor = windowSystem->getCompositor();
            const RegionHandle emptyInputRegion { WaylandProtocol::wlCompositorCreateRegion (compositor) };

            if (emptyInputRegion != nullptr)
                WaylandProtocol::wlSurfaceSetInputRegion (surface.get(), emptyInputRegion.get());
        }

        windowTitle = component.getName();

        if (const auto popupKind = getWaylandPopupKind (windowStyleFlags))
            surfaceRole = PopupRole { *popupKind };
        else if (! createToplevel())
            return;

        repainter = std::make_unique<WaylandRepaintManager> (*this);

        // A peer created for a component that already has an alpha never receives a setAlpha call.
        repainter->setWindowAlpha (comp.getAlpha());

        // No repaint or listener notification is needed while the peer is being constructed.
        surfaceScale.setOutputScale (getScaleForSurfaceOutputs());
        createScaleObjects();
        updateToplevelSizeConstraints();
        WaylandProtocol::wlSurfaceAddListener (surface.get(), &surfaceListener, callbackState.get());
        windowSystem->addInputListener (surface.get(), *this);
        windowSystem->addDataDeviceListener (surface.get(), *this);
        windowSystem->addOutputListener (*this);
        diagnostics.isWaylandBackend = true;
    }

    ~WaylandComponentPeer() override
    {
        JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED

        // Listener data stays reachable until each proxy is destroyed. Clear the weak
        // peer first so queued callbacks no-op before callbackState is freed.
        if (callbackState != nullptr)
            callbackState->peer = nullptr;

        WaylandWindowSystem::getInstance()->removeInputListener (*this);
        WaylandWindowSystem::getInstance()->removeDataDeviceListener (*this);
        WaylandWindowSystem::getInstance()->removeOutputListener (*this);

        // This destroys wl_buffers the compositor may still hold, before the surface. SUD-143.
        repainter = nullptr;

        frameCallback.reset();
        pendingActivationToken.reset();
        endPopupGrab();

        // Destroy the popup or toplevel before the wl_surface they were created from.
        surfaceRole = {};

        scaleObjects.reset();
        surface.reset();

        callbackState = nullptr;
        WaylandWindowSystem::getInstance()->flush();
    }

    void* getNativeHandle() const override
    {
        return surface.get();
    }

    void setVisible (bool shouldBeVisible) override
    {
        visible = shouldBeVisible;

        if (surface == nullptr)
            return;

        if (visible)
        {
            // A wl_surface cannot change roles, so a popup that cannot be created falls back
            // to a freestanding toplevel for the lifetime of this peer.
            if (auto* popupRole = getPopupRole(); popupRole != nullptr
                && popupRole->popup == nullptr
                && ! createPopup (*popupRole))
            {
                surfaceRole = ToplevelRole{};
                diagnostics.usesPopupRole = false;
            }

            if (auto* popup = getPopup())
            {
                popup->show();
            }
            else
            {
                if (getToplevel() == nullptr && ! createToplevel())
                    return;

                getToplevel()->show();
            }

            // A full repaint drives the next commit, which maps the surface.
            repaint (logicalBounds.withZeroOrigin());
        }
        else if (getToplevel() != nullptr || getPopup() != nullptr)
        {
            // Weston does not configure a surface remapped after an empty-buffer unmap,
            // so destroy and recreate its xdg_popup or xdg_toplevel.
            // A pending frame callback belongs to the old mapping and may never arrive.
            frameCallback.reset();
            endPopupGrab();

            if (auto* popupRole = getPopupRole())
                popupRole->popup.reset();
            else if (auto* toplevelRole = getToplevelRole())
                toplevelRole->toplevel.reset();

            // Creating the next xdg_surface needs a wl_surface with no committed buffer.
            WaylandProtocol::wlSurfaceAttach (surface.get(), nullptr, 0, 0);
            WaylandProtocol::wlSurfaceCommit (surface.get());
            WaylandWindowSystem::getInstance()->flush();
            ++diagnostics.unmapCommits;

            configured = false;
            hasCommittedBuffer = false;

            // The compositor discards all toplevel state on unmap.
            if (getToplevelRole() != nullptr)
            {
                fullScreenState.toplevelDestroyed();
                wantsMinimised = false;
            }
        }
    }

    void setTitle (const String& title) override
    {
        windowTitle = title;

        if (auto* toplevel = getToplevel())
            toplevel->setTitle (title);
    }

    void setBounds (const Rectangle<int>& newBounds, bool) override
    {
        const auto corrected = newBounds.withSize (jmax (1, newBounds.getWidth()),
                                                   jmax (1, newBounds.getHeight()));

        if (logicalBounds == corrected)
        {
            if (updateToplevelSizeConstraints())
                getToplevel()->commitSizeConstraints (getSurfaceContentSize());

            return;
        }

        const auto movedWithoutResizing = corrected.withZeroOrigin() == logicalBounds.withZeroOrigin();
        logicalBounds = corrected;
        updateToplevelSizeConstraints();

        if (auto* popupRole = getPopupRole(); popupRole != nullptr && popupRole->popup != nullptr)
        {
            const auto placement = makeWaylandPopupPlacement (logicalBounds, popupRole->parent.parentCoordinates);

            if (popupRole->popup->reposition (placement))
            {
                // The configure response supplies the accepted bounds and starts the repaint.
                return;
            }
        }

        if (configured)
        {
            // ComponentPeer has no separate notification for a JUCE title-bar drag. For windows
            // without native decorations, treat a position-only bounds change during a held press
            // as an interactive move.
            if (! hasNativeTitleBar()
                && movedWithoutResizing
                && startInteractiveMoveOrResize (WaylandProtocol::xdgToplevelResizeEdgeNone))
            {
                handleMovedOrResized();
                return;
            }

            if (auto* toplevel = getToplevel())
                toplevel->contentResized (getSurfaceContentSize());

            handleMovedOrResized();
            repaint (logicalBounds.withZeroOrigin());
        }
    }

    Rectangle<int> getBounds() const override
    {
        return logicalBounds;
    }

    OptionalBorderSize getFrameSizeIfPresent() const override
    {
        if (auto* toplevel = getToplevel())
            return toplevel->getFrameSizeIfPresent();

        return OptionalBorderSize { BorderSize<int>() };
    }

    BorderSize<int> getFrameSize() const override
    {
        return {};
    }

    using ComponentPeer::localToGlobal;
    using ComponentPeer::globalToLocal;

    Point<float> localToGlobal (Point<float> point) override
    {
        // Wayland has no global window coordinates, so use the requested logical bounds (SUD-129).
        return point + logicalBounds.getPosition().toFloat();
    }

    Point<float> globalToLocal (Point<float> point) override
    {
        // Use the inverse of the requested-bounds coordinate approximation above (SUD-129).
        return point - logicalBounds.getPosition().toFloat();
    }

    StringArray getAvailableRenderingEngines() override
    {
        // Shared-memory software rendering is the only available rendering path (SUD-161).
        return { "Software Renderer" };
    }

    void setMinimised (bool shouldBeMinimised) override
    {
        auto* toplevel = getToplevel();

        // xdg-shell has no request to restore a minimised window.
        if (! shouldBeMinimised || toplevel == nullptr)
            return;

        toplevel->requestMinimise();
        wantsMinimised = true;
    }

    bool isMinimised() const override
    {
        return wantsMinimised;
    }

    bool isShowing() const override
    {
        return visible && configured && ! wantsMinimised;
    }

    void setFullScreen (bool shouldBeFullScreen) override
    {
        fullScreenState.setFullScreenRequested (shouldBeFullScreen);

        if (auto* toplevel = getToplevel())
            toplevel->requestFullScreen (shouldBeFullScreen);
    }

    bool isFullScreen() const override
    {
        return fullScreenState.isFullScreen();
    }

    void startHostManagedResize (Point<int>, ResizableBorderComponent::Zone zone) override
    {
        if (updateToplevelSizeConstraints())
            getToplevel()->commitSizeConstraints (getSurfaceContentSize());

        startInteractiveMoveOrResize (getXdgResizeEdgeForZone (zone));
    }

    bool contains (Point<int> localPos, bool) const override
    {
        // This peer has no native subsurfaces, so local-bounds hit-testing is sufficient (SUD-129).
        return logicalBounds.withZeroOrigin().contains (localPos);
    }

    void toFront (bool makeActive) override
    {
        // Wayland cannot arbitrarily raise a surface, so update JUCE's window bookkeeping only
        // (SUD-129).
        if (makeActive)
            grabFocus();

        handleBroughtToFront();
    }

    void toBehind (ComponentPeer*) override
    {
        // Wayland has no request for placing one surface behind another (SUD-129).
    }

    bool isFocused() const override
    {
        return focused;
    }

    void grabFocus() override
    {
        if (focused || getToplevel() == nullptr)
            return;

        auto* windowSystem = WaylandWindowSystem::getInstance();
        auto* activation = windowSystem->getXdgActivation();

        if (activation == nullptr)
            return;

        pendingActivationToken.reset (WaylandProtocol::xdgActivationV1GetActivationToken (activation));

        if (pendingActivationToken == nullptr)
            return;

        WaylandProtocol::xdgActivationTokenV1AddListener (pendingActivationToken.get(), &activationTokenListener, callbackState.get());

        const auto inputEvent = windowSystem->getLatestInputSerial();

        if (inputEvent.has_value())
        {
            if (auto* seat = windowSystem->getSeat())
                WaylandProtocol::xdgActivationTokenV1SetSerial (pendingActivationToken.get(), inputEvent->value, seat);

            if (inputEvent->sourceSurface != nullptr)
                WaylandProtocol::xdgActivationTokenV1SetSurface (pendingActivationToken.get(), inputEvent->sourceSurface);
        }

        WaylandProtocol::xdgActivationTokenV1Commit (pendingActivationToken.get());
        windowSystem->flush();
    }

    void repaint (const Rectangle<int>& area) override
    {
        if (repainter != nullptr)
            repainter->repaint (area.getIntersection (logicalBounds.withZeroOrigin()));
    }

    void performAnyPendingRepaintsNow() override
    {
        if (repainter != nullptr)
            repainter->performAnyPendingRepaintsNow();
    }

    void setIcon (const Image&) override
    {
        // This backend does not implement a toplevel icon protocol (SUD-126).
    }

    double getPlatformScaleFactor() const noexcept override
    {
        return surfaceScale.getScaleFactor();
    }

    void setCustomPlatformScaleFactor (std::optional<double> scaleIn) override
    {
        handleScaleUpdate (surfaceScale.setOverrideScale (scaleIn));
    }

    std::optional<double> getCustomPlatformScaleFactor() const override
    {
        return surfaceScale.getOverrideScale();
    }

    void setAlpha (float newAlpha) override
    {
        if (repainter != nullptr)
            repainter->setWindowAlpha (newAlpha);
    }

    bool setAlwaysOnTop (bool) override
    {
        // Wayland has no request for always-on-top state. Report success because a false
        // return makes Component recreate the window (SUD-129).
        return true;
    }

    void textInputRequired (Point<int>, TextInputTarget&) override
    {
        // Text input requires a zwp_text_input implementation, which is not available yet
        // (SUD-123).
    }

    std::unique_ptr<WaylandOpenGLSurface> createOpenGLSurface (Component& target);

#if JUCE_WAYLAND_PEER_DIAGNOSTICS
    detail::WaylandPeerDiagnostics getDiagnostics() const
    {
        auto result = diagnostics;

        auto* windowSystem = WaylandWindowSystem::getInstance();
        result.registryGlobalsBound = windowSystem->areRegistryGlobalsBound();
        result.seatBound = windowSystem->isSeatBound();
        result.keyboardBound = windowSystem->isKeyboardBound();
        result.pointerBound = windowSystem->isPointerBound();
        result.touchBound = windowSystem->isTouchBound();
        result.surfaceScale = getPlatformScaleFactor();
        result.fractionalScaleActive = scaleObjects.has_value() && scaleObjects->hasFractionalScale();
        result.boundOutputs = windowSystem->getNumBoundOutputs();
        result.enteredOutputs = surfaceOutputs.size();

        if (repainter != nullptr)
        {
            result.bufferPoolSize = repainter->getBufferPoolSize();
            result.busyBuffers = repainter->getBusyBufferCount();
        }

        return result;
    }
#endif

private:
    using ViewportHandle = std::unique_ptr<wp_viewport, FunctionPointerDestructor<WaylandProtocol::wpViewportDestroy>>;
    using FractionalScaleHandle = std::unique_ptr<wp_fractional_scale_v1,
                                                  FunctionPointerDestructor<WaylandProtocol::wpFractionalScaleV1Destroy>>;
    using AlphaModifierSurfaceHandle = std::unique_ptr<wp_alpha_modifier_surface_v1,
                                                       FunctionPointerDestructor<WaylandProtocol::wpAlphaModifierSurfaceV1Destroy>>;
    using RegionHandle = std::unique_ptr<wl_region, FunctionPointerDestructor<WaylandProtocol::wlRegionDestroy>>;

    struct PopupGrab
    {
        WeakReference<Component> componentToDismiss;
        uint32_t serial;
    };

    struct ToplevelRole
    {
        std::unique_ptr<WaylandToplevel> toplevel;
        std::optional<WaylandSizeConstraints> sizeConstraints;
    };

    struct PopupRole
    {
        explicit PopupRole (WaylandPopupKind kindIn) : kind (kindIn) {}

        WaylandPopupKind kind;
        WaylandPopupParent parent;
        std::unique_ptr<WaylandPopup> popup;
        std::optional<PopupGrab> grab;
    };

    ToplevelRole* getToplevelRole() noexcept { return std::get_if<ToplevelRole> (&surfaceRole); }
    const ToplevelRole* getToplevelRole() const noexcept { return std::get_if<ToplevelRole> (&surfaceRole); }
    PopupRole* getPopupRole() noexcept { return std::get_if<PopupRole> (&surfaceRole); }
    const PopupRole* getPopupRole() const noexcept { return std::get_if<PopupRole> (&surfaceRole); }

    WaylandToplevel* getToplevel() const noexcept
    {
        auto* toplevelRole = getToplevelRole();
        return toplevelRole != nullptr ? toplevelRole->toplevel.get() : nullptr;
    }

    WaylandPopup* getPopup() const noexcept
    {
        auto* popupRole = getPopupRole();
        return popupRole != nullptr ? popupRole->popup.get() : nullptr;
    }

    class OpenGLSurface final : public WaylandOpenGLSurface
    {
    public:
        using SurfaceHandle = std::unique_ptr<wl_surface,
                                              FunctionPointerDestructor<WaylandProtocol::wlSurfaceDestroy>>;
        using SubsurfaceHandle = std::unique_ptr<wl_subsurface,
                                                 FunctionPointerDestructor<WaylandProtocol::wlSubsurfaceDestroy>>;

        static std::unique_ptr<WaylandOpenGLSurface> create (WaylandComponentPeer& peer, Component& component)
        {
            auto* windowSystem = WaylandWindowSystem::getInstance();
            auto* compositor = windowSystem->getCompositor();
            auto* subcompositor = windowSystem->getSubcompositor();

            if (compositor == nullptr || subcompositor == nullptr || peer.surface == nullptr)
                return {};

            SurfaceHandle surface { WaylandProtocol::wlCompositorCreateSurface (compositor) };

            if (surface == nullptr)
                return {};

            SubsurfaceHandle subsurface { WaylandProtocol::wlSubcompositorGetSubsurface (subcompositor,
                                                                                         surface.get(),
                                                                                         peer.surface.get()) };

            if (subsurface == nullptr)
                return {};

            ViewportHandle viewport;

            if (peer.getBufferMappingMethod() == WaylandSurfaceScale::BufferMappingMethod::viewport)
            {
                viewport.reset (WaylandProtocol::wpViewporterGetViewport (windowSystem->getViewporter(),
                                                                          surface.get()));

                if (viewport == nullptr)
                    return {};
            }

            const RegionHandle emptyInputRegion { WaylandProtocol::wlCompositorCreateRegion (compositor) };

            if (emptyInputRegion == nullptr)
                return {};

            WaylandProtocol::wlSurfaceSetInputRegion (surface.get(), emptyInputRegion.get());
            WaylandProtocol::wlSubsurfaceSetDesync (subsurface.get());

            return rawToUniquePtr (new OpenGLSurface (peer,
                                                      component,
                                                      *windowSystem,
                                                      std::move (surface),
                                                      std::move (subsurface),
                                                      std::move (viewport)));
        }

        ~OpenGLSurface() override
        {
            // The GL thread has stopped and this runs on the message thread, so a frame
            // callback cannot be created or completed concurrently.
            frameCallback.reset();
            viewport.reset();
            subsurface.reset();
            surface.reset();
            windowSystem.flush();
        }

        wl_display* getDisplay() const noexcept override { return windowSystem.getDisplay(); }
        wl_surface* getSurface() const noexcept override { return surface.get(); }

        Point<int> updateBounds() override
        {
            if (component.getPeer() != &peer)
                return {};

            const auto logicalBounds = peer.getAreaCoveredBy (component);

            if (logicalBounds.isEmpty())
                return {};

            const auto mappingMethod = peer.getBufferMappingMethod();
            const auto parentLogicalBounds = peer.logicalBounds.withZeroOrigin();
            const auto parentSurfaceSize = peer.getSurfaceContentSize();
            const auto surfaceBounds = WaylandSurfaceScale::mapLogicalRectToSurface (logicalBounds,
                                                                                     parentLogicalBounds,
                                                                                     { parentSurfaceSize.x, parentSurfaceSize.y });
            const auto geometry = peer.surfaceScale.getBufferGeometry (logicalBounds.withZeroOrigin(),
                                                                       mappingMethod);

            if (lastSurfaceBounds != surfaceBounds)
            {
                WaylandProtocol::wlSubsurfaceSetPosition (subsurface.get(),
                                                          surfaceBounds.getX(),
                                                          surfaceBounds.getY());

                // Subsurface position state takes effect with the next parent commit. Repaint the
                // covered area so it is applied with a complete parent buffer update.
                peer.repaint (lastLogicalBounds.has_value() ? logicalBounds.getUnion (*lastLogicalBounds)
                                                            : logicalBounds);
                lastSurfaceBounds = surfaceBounds;
            }

            lastLogicalBounds = logicalBounds;

            WaylandProtocol::wlSurfaceSetBufferScale (surface.get(), geometry.bufferScale);

            if (viewport != nullptr)
            {
                WaylandProtocol::wpViewportSetDestination (viewport.get(),
                                                           jmax (1, surfaceBounds.getWidth()),
                                                           jmax (1, surfaceBounds.getHeight()));

                return { jmax (1, geometry.bufferBounds.getWidth()),
                         jmax (1, geometry.bufferBounds.getHeight()) };
            }

            // Without a viewport, bufferScale is the only mapping from buffer pixels to
            // surface coordinates. Use the bounds rounded within the parent so adjacent
            // subsurfaces share the same edge without a gap or overlap.
            return { jmax (1, surfaceBounds.getWidth()  * geometry.bufferScale),
                     jmax (1, surfaceBounds.getHeight() * geometry.bufferScale) };
        }

        bool requestFrameCallback (std::function<void()> callback) override
        {
            const std::scoped_lock lock { frameCallbackMutex };

            if (frameCallback != nullptr)
                return false;

            frameCallback.reset (WaylandProtocol::wlSurfaceFrame (surface.get()));

            if (frameCallback == nullptr)
                return false;

            frameCallbackFunction = std::move (callback);

            if (WaylandProtocol::wlCallbackAddListener (frameCallback.get(), &frameListener, this) != 0)
            {
                frameCallback.reset();
                frameCallbackFunction = {};
                return false;
            }

            return true;
        }

    private:
        using FrameCallbackHandle = std::unique_ptr<wl_callback,
                                                    FunctionPointerDestructor<WaylandProtocol::wlCallbackDestroy>>;

        OpenGLSurface (WaylandComponentPeer& peerIn,
                       Component& componentIn,
                       WaylandWindowSystem& windowSystemIn,
                       SurfaceHandle surfaceIn,
                       SubsurfaceHandle subsurfaceIn,
                       ViewportHandle viewportIn)
            : peer (peerIn),
              component (componentIn),
              windowSystem (windowSystemIn),
              surface (std::move (surfaceIn)),
              subsurface (std::move (subsurfaceIn)),
              viewport (std::move (viewportIn))
        {
        }

        void handleFrameDone (wl_callback* callback)
        {
            std::function<void()> function;

            {
                const std::scoped_lock lock { frameCallbackMutex };

                if (callback != frameCallback.get())
                    return;

                frameCallback.reset();
                function = std::move (frameCallbackFunction);
            }

            NullCheckedInvocation::invoke (function);
        }

        static const wl_callback_listener frameListener;

        WaylandComponentPeer& peer;
        Component& component;
        WaylandWindowSystem& windowSystem;
        SurfaceHandle surface;
        SubsurfaceHandle subsurface;
        ViewportHandle viewport;
        std::mutex frameCallbackMutex;
        FrameCallbackHandle frameCallback;
        std::function<void()> frameCallbackFunction;
        std::optional<Rectangle<int>> lastLogicalBounds;
        std::optional<Rectangle<int>> lastSurfaceBounds;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenGLSurface)
    };

    class ScaleObjects final
    {
    public:
        ScaleObjects (ViewportHandle viewportIn, FractionalScaleHandle fractionalScaleIn)
            : viewport (std::move (viewportIn)),
              fractionalScale (std::move (fractionalScaleIn))
        {
            // This object represents viewport-backed mapping, so it is invalid without a viewport.
            jassert (viewport != nullptr);
        }

        wp_viewport* getViewport() const noexcept { return viewport.get(); }
        bool hasFractionalScale() const noexcept { return fractionalScale != nullptr; }

    private:
        ViewportHandle viewport;
        FractionalScaleHandle fractionalScale;
    };

    class WaylandRepaintManager final : private AsyncUpdater,
                                        private Timer
    {
    public:
        explicit WaylandRepaintManager (WaylandComponentPeer& p)
            : peer (p) {}

        void repaint (Rectangle<int> area)
        {
            if (area.isEmpty())
                return;

            ++peer.diagnostics.repaintsRequested;

            // Repaint regions remain in logical coordinates until the next frame is painted.
            regionsNeedingRepaint.add (area);

            // handleFrameDone() processes queued regions after the pending frame callback arrives.
            if (peer.frameCallback == nullptr)
                triggerAsyncUpdate();
        }

        bool hasPendingRepaints() const { return ! regionsNeedingRepaint.isEmpty(); }

        void performAnyPendingRepaintsNow()
        {
            if (! peer.configured || ! peer.visible || peer.surface == nullptr)
                return;

            // Wait until the compositor allows another frame.
            if (peer.frameCallback != nullptr)
                return;

            if (regionsNeedingRepaint.isEmpty())
                return;

            const auto logicalBounds = peer.logicalBounds.withZeroOrigin();
            const auto geometry = peer.surfaceScale.getBufferGeometry (logicalBounds, peer.getBufferMappingMethod());
            const auto bufferBounds = geometry.bufferBounds;

            if (bufferBounds.isEmpty())
                return;

            auto* buffer = acquireBuffer (bufferBounds.getWidth(), bufferBounds.getHeight());

            // Keep repaint regions queued until handleBufferRelease() makes a buffer available
            // (SUD-120).
            if (buffer == nullptr)
            {
                ++peer.diagnostics.commitsDeferredNoBuffer;
                return;
            }

            cancelPendingUpdate();

            const auto dirtyRegions = regionsNeedingRepaint;
            regionsNeedingRepaint.clear();

            const auto scratchImageWasRecreated = ensureScratchImage (bufferBounds);
            const auto knownStaleRegions = bufferPool.getKnownStaleRegions (*buffer);

            const auto logicalPaintRegions = computeRegionsToPaint (dirtyRegions, knownStaleRegions,
                                                                    scratchImageWasRecreated, logicalBounds);

            const auto bufferPaintRegions = mapLogicalRegionsToBuffer (logicalPaintRegions, logicalBounds, bufferBounds);

            paintIntoScratchImage (bufferPaintRegions, bufferBounds, logicalBounds);

            // Without the alpha modifier the window alpha has to be baked into the pixels.
            const auto alphaMultiplier = std::invoke ([&]() -> std::optional<uint8>
            {
                if (alphaModifierSurface != nullptr || 1.0f <= windowAlpha)
                    return std::nullopt;

                return (uint8) roundToInt (windowAlpha * 255.0f);
            });

            copyImageRegionsToWaylandShmBuffer (scratchImage, *buffer, bufferPaintRegions, alphaMultiplier);

            const auto bufferDamageRegions = computeRegionsToReportAsDamage (
                mapLogicalRegionsToBuffer (dirtyRegions, logicalBounds, bufferBounds),
                knownStaleRegions, bufferBounds);

            recordCommitDiagnostics (bufferDamageRegions, bufferBounds);
            bufferPool.recordCommit (*buffer, dirtyRegions);

            submitFrame (geometry, *buffer, bufferDamageRegions);

            startTimer (3000);
        }

        void setWindowAlpha (float newAlpha)
        {
            const auto clamped = jlimit (0.0f, 1.0f, newAlpha);

            if (approximatelyEqual (clamped, windowAlpha))
                return;

            const auto hadBakedAlpha = alphaModifierSurface == nullptr && windowAlpha < 1.0f;

            windowAlpha = clamped;

            if (! createAlphaModifierSurfaceIfNeeded())
            {
                // Without the protocol the alpha is multiplied into the buffer while blitting, so
                // every pixel of the window changes.
                repaint (peer.logicalBounds.withZeroOrigin());
                return;
            }

            const auto factor = std::round ((double) clamped * (double) std::numeric_limits<uint32_t>::max());
            WaylandProtocol::wpAlphaModifierSurfaceV1SetMultiplier (alphaModifierSurface.get(), (uint32_t) factor);

            if (hadBakedAlpha)
            {
                // Buffers painted before the modifier existed still have alpha baked into their pixels,
                // so a repaint replaces them in the same commit that applies the multiplier.
                repaint (peer.logicalBounds.withZeroOrigin());
                return;
            }

            // The compositor applies the new window alpha on the next surface commit, so an idle
            // window needs a commit of its own.
            if (peer.configured && peer.visible && peer.surface != nullptr)
            {
                updateOpaqueRegionIfChanged();
                ++peer.diagnostics.commitsSubmitted;
                WaylandProtocol::wlSurfaceCommit (peer.surface.get());
                WaylandWindowSystem::getInstance()->flush();
            }
        }

        int getBufferPoolSize() const noexcept      { return bufferPool.size(); }
        int getBusyBufferCount() const noexcept     { return bufferPool.busyCount(); }

    private:
        void handleAsyncUpdate() override
        {
            performAnyPendingRepaintsNow();
        }

        void timerCallback() override
        {
            // Like X11 does, free the scratch image when the window has been idle
            stopTimer();
            scratchImage = Image();
        }

        // Asking the compositor for a second alpha-modifier surface for the same wl_surface is a
        // protocol error, so the handle is created once and kept.
        bool createAlphaModifierSurfaceIfNeeded()
        {
            if (alphaModifierSurface != nullptr)
                return true;

            auto* alphaModifier = WaylandWindowSystem::getInstance()->getAlphaModifier();

            if (alphaModifier == nullptr || peer.surface == nullptr)
                return false;

            alphaModifierSurface.reset (WaylandProtocol::wpAlphaModifierV1GetSurface (alphaModifier, peer.surface.get()));
            return alphaModifierSurface != nullptr;
        }

        void updateOpaqueRegionIfChanged()
        {
            auto* compositor = WaylandWindowSystem::getInstance()->getCompositor();

            if (peer.surface == nullptr || compositor == nullptr)
                return;

            const auto shouldBeOpaque = peer.getComponent().isOpaque() && 1.0f <= windowAlpha;

            if (lastCommittedOpaque == shouldBeOpaque)
                return;

            if (! shouldBeOpaque)
            {
                WaylandProtocol::wlSurfaceSetOpaqueRegion (peer.surface.get(), nullptr);
                lastCommittedOpaque = shouldBeOpaque;
                return;
            }

            // set_opaque_region copies the region, so the handle can go out of scope straight away.
            const RegionHandle region { WaylandProtocol::wlCompositorCreateRegion (compositor) };

            if (region == nullptr)
                return;

            // The compositor clips the opaque region to the surface. An oversized rectangle avoids
            // the fractional-scale case, where the buffer rounds up and a computed rectangle would
            // either miss or overclaim the last row and column.
            WaylandProtocol::wlRegionAdd (region.get(), 0, 0,
                                          std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
            WaylandProtocol::wlSurfaceSetOpaqueRegion (peer.surface.get(), region.get());

            lastCommittedOpaque = shouldBeOpaque;
        }

        [[nodiscard]] WaylandShmBuffer* acquireBuffer (int width, int height)
        {
            if (auto* existing = bufferPool.acquire (width, height))
                return existing;

            if (bufferPool.isFull())
                return nullptr;

            auto* windowSystem = WaylandWindowSystem::getInstance();
            auto* shm = windowSystem->getShm();

            if (shm == nullptr)
                return nullptr;

            auto buffer = WaylandShmBuffer::create (*shm, width, height);

            if (buffer == nullptr)
                return nullptr;

            WaylandProtocol::wlBufferAddListener (buffer->handle.get(), &bufferReleaseListener, this);

            if (auto* added = bufferPool.add (std::move (buffer)))
            {
                ++peer.diagnostics.buffersCreatedTotal;
                return added;
            }

            return nullptr;
        }

        static RectangleList<int> mapLogicalRegionsToBuffer (const RectangleList<int>& logicalRegions,
                                                             Rectangle<int> logicalBounds,
                                                             Rectangle<int> bufferBounds)
        {
            RectangleList<int> result;

            for (const auto& region : logicalRegions)
            {
                const auto mapped = WaylandSurfaceScale::mapLogicalRectToBuffer (region, logicalBounds, bufferBounds);
                result.add (mapped.getIntersection (bufferBounds));
            }

            return result;
        }

        [[nodiscard]] bool ensureScratchImage (Rectangle<int> bufferBounds)
        {
            const auto needsRecreating = scratchImage.isNull()
                                      || scratchImage.getWidth() < bufferBounds.getWidth()
                                      || scratchImage.getHeight() < bufferBounds.getHeight();

            if (needsRecreating)
                scratchImage = Image (Image::ARGB, bufferBounds.getWidth(), bufferBounds.getHeight(), false);

            return needsRecreating;
        }

        void paintIntoScratchImage (const RectangleList<int>& paintRegions,
                                    Rectangle<int> bufferBounds,
                                    Rectangle<int> logicalBounds)
        {
            for (const auto& region : paintRegions)
                scratchImage.clear (region);

            auto context = peer.getComponent().getLookAndFeel()
                            .createGraphicsContext (scratchImage, Point<int>(), paintRegions);

            // Map logical component coordinates across the whole rounded buffer.
            context->addTransform (AffineTransform::scale ((float) bufferBounds.getWidth() / (float) logicalBounds.getWidth(),
                                                           (float) bufferBounds.getHeight() / (float) logicalBounds.getHeight()));
            peer.handlePaint (*context);
        }

        void submitFrame (const WaylandSurfaceScale::BufferGeometry& geometry,
                          WaylandShmBuffer& buffer,
                          const RectangleList<int>& damageRegions)
        {
            WaylandProtocol::wlSurfaceSetBufferScale (peer.surface.get(), geometry.bufferScale);

            if (geometry.viewportDestination.has_value())
            {
                // A viewport destination is only produced when scaleObjects exists.
                WaylandProtocol::wpViewportSetDestination (peer.scaleObjects->getViewport(),
                                                           geometry.viewportDestination->getWidth(),
                                                           geometry.viewportDestination->getHeight());
            }

            updateOpaqueRegionIfChanged();

            WaylandProtocol::wlSurfaceAttach (peer.surface.get(), buffer.handle.get(), 0, 0);

            for (const auto& region : damageRegions)
                WaylandProtocol::wlSurfaceDamageBuffer (peer.surface.get(), region.getX(), region.getY(), region.getWidth(), region.getHeight());

            peer.requestFrameCallback();
            WaylandProtocol::wlSurfaceCommit (peer.surface.get());
            peer.hasCommittedBuffer = true;
            WaylandWindowSystem::getInstance()->flush();
        }

        void recordCommitDiagnostics (const RectangleList<int>& damageRegions, Rectangle<int> bufferBounds)
        {
            ++peer.diagnostics.commitsSubmitted;
            peer.diagnostics.lastCommitDamageRectCount = damageRegions.getNumRectangles();
            peer.diagnostics.lastCommitDamageArea = 0;
            peer.diagnostics.lastCommitBufferWidth = bufferBounds.getWidth();
            peer.diagnostics.lastCommitBufferHeight = bufferBounds.getHeight();

            for (const auto& region : damageRegions)
                peer.diagnostics.lastCommitDamageArea += (int64) region.getWidth() * (int64) region.getHeight();
        }

        void handleBufferRelease (wl_buffer* released)
        {
            bufferPool.markIdleIf ([released] (const WaylandShmBuffer& buffer)
            {
                return buffer.handle.get() == released;
            });
            performAnyPendingRepaintsNow();
        }

        static const wl_buffer_listener bufferReleaseListener;

        WaylandComponentPeer& peer;
        WaylandShmBufferPool bufferPool;
        RectangleList<int> regionsNeedingRepaint;
        Image scratchImage;
        AlphaModifierSurfaceHandle alphaModifierSurface;
        std::optional<bool> lastCommittedOpaque;
        float windowAlpha = 1.0f;

        JUCE_DECLARE_NON_COPYABLE (WaylandRepaintManager)
    };

    bool hasNativeTitleBar() const
    {
        return (getStyleFlags() & windowHasTitleBar) != 0;
    }

    bool createToplevel()
    {
        auto* toplevelRole = getToplevelRole();

        if (toplevelRole == nullptr)
            return false;

        const auto nativeTitleBar = hasNativeTitleBar() ? WaylandToplevel::NativeTitleBar::yes
                                                        : WaylandToplevel::NativeTitleBar::no;

        toplevelRole->sizeConstraints.reset();
        toplevelRole->toplevel = createWaylandToplevel (*this, diagnostics, *surface, windowTitle, nativeTitleBar,
                                                        fullScreenState.isFullScreenRequested());
        updateToplevelSizeConstraints();
        return toplevelRole->toplevel != nullptr;
    }

    bool startInteractiveMoveOrResize (uint32_t resizeEdge)
    {
        auto* toplevel = getToplevel();

        if (toplevel == nullptr)
            return false;

        auto* windowSystem = WaylandWindowSystem::getInstance();
        auto* seat = windowSystem->getSeat();
        const auto serial = windowSystem->getHeldPressSerial (surface.get());

        if (seat == nullptr || ! serial.has_value())
            return false;

        if (resizeEdge == WaylandProtocol::xdgToplevelResizeEdgeNone)
            toplevel->requestInteractiveMove (*seat, *serial);
        else
            toplevel->requestInteractiveResize (*seat, *serial, resizeEdge);

        windowSystem->dragHandedToCompositor (surface.get());
        windowSystem->flush();
        return true;
    }

    bool createPopup (PopupRole& popupRole)
    {
        auto* windowSystem = WaylandWindowSystem::getInstance();
        const auto isInteractive = popupRole.kind == WaylandPopupKind::interactive;

        // Resolve the parent and grab serial on every map because both may have become stale.
        if (auto refreshed = windowSystem->findPopupParent (popupRole.kind))
            popupRole.parent = std::move (*refreshed);
        else
            return false;

        auto& parent = popupRole.parent;

        if (parent.parentXdgSurface == nullptr)
            return false;

        // Popup components may recreate their peer before becoming visible, for example when
        // setOpaque() changes. Claim the serial only when the popup maps and a seat exists.
        const auto grabSerial = std::invoke ([&]() -> std::optional<uint32_t>
        {
            if (! isInteractive || parent.grabSerial == nullptr || windowSystem->getSeat() == nullptr)
                return std::nullopt;

            return parent.grabSerial->claim();
        });

        const auto placement = makeWaylandPopupPlacement (logicalBounds, parent.parentCoordinates);
        popupRole.popup = WaylandPopup::create (*this,
                                                *surface,
                                                *parent.parentXdgSurface,
                                                placement,
                                                grabSerial);

        if (popupRole.popup == nullptr)
            return false;

        diagnostics.usesPopupRole = true;
        diagnostics.popupHasGrab = grabSerial.has_value();

        if (grabSerial.has_value())
        {
            auto* componentToDismiss = parent.componentToDismiss != nullptr ? parent.componentToDismiss
                                                                            : &component;
            popupRole.grab = PopupGrab { componentToDismiss, *grabSerial };
            windowSystem->popupGrabStarted (parent.grabOwnerSurface, *componentToDismiss);
        }

        return true;
    }

    void endPopupGrab()
    {
        if (auto* popupRole = getPopupRole(); popupRole != nullptr && popupRole->grab.has_value())
        {
            popupRole->grab.reset();
            diagnostics.popupHasGrab = false;
            WaylandWindowSystem::getInstance()->popupGrabEnded (popupRole->parent.grabOwnerSurface);
        }
    }

    Point<int> getSurfaceContentSize() const
    {
        const auto surfaceSize = surfaceScale.getSurfaceSize (logicalBounds.withZeroOrigin(),
                                                              getBufferMappingMethod());
        return { jmax (1, roundToInt (surfaceSize.x)), jmax (1, roundToInt (surfaceSize.y)) };
    }

    Point<int> getSurfaceConstraintSize (Point<int> logicalSize) const
    {
        const auto scale = surfaceScale.getLogicalToSurfaceScale (getBufferMappingMethod());
        const auto convert = [scale] (int extent)
        {
            if (extent <= 0)
                return 0;

            return (int) std::round (jlimit (1.0,
                                             (double) std::numeric_limits<int32_t>::max(),
                                             extent * scale));
        };

        return { convert (logicalSize.x), convert (logicalSize.y) };
    }

    bool isToplevelResizable()
    {
        if ((getStyleFlags() & windowIsResizable) != 0)
            return true;

        // JUCE-drawn resize controls do not request a compositor-drawn resizable frame.
        if (const auto* window = dynamic_cast<const ResizableWindow*> (&getComponent()))
            return window->isResizable();

        return false;
    }

    bool updateToplevelSizeConstraints()
    {
        auto* toplevelRole = getToplevelRole();

        if (toplevelRole == nullptr || toplevelRole->toplevel == nullptr)
            return false;

        const auto constraints = std::invoke ([&]() -> WaylandSizeConstraints
        {
            if (! isToplevelResizable())
            {
                const auto fixedSize = getSurfaceContentSize();
                return { fixedSize, fixedSize };
            }

            if (const auto* constrainer = getConstrainer())
            {
                const auto logical = getWaylandSizeConstraints (*constrainer);
                const auto minimum = getSurfaceConstraintSize (logical.minimum);
                auto maximum = getSurfaceConstraintSize (logical.maximum);

                if (maximum.x != 0)
                    maximum.x = jmax (minimum.x, maximum.x);

                if (maximum.y != 0)
                    maximum.y = jmax (minimum.y, maximum.y);

                return { minimum, maximum };
            }

            return {};
        });

        if (toplevelRole->sizeConstraints.has_value() && *toplevelRole->sizeConstraints == constraints)
            return false;

        toplevelRole->toplevel->setSizeConstraints (constraints);
        toplevelRole->sizeConstraints = constraints;
        return true;
    }

    int getScaleForSurfaceOutputs() const
    {
        auto* windowSystem = WaylandWindowSystem::getInstance();

        return surfaceOutputs.getLargestScale (windowSystem->getLargestOutputScale(),
                                               [windowSystem] (wl_output* output) { return windowSystem->getScaleForOutput (output); });
    }

    void updateOutputScale()
    {
        const auto update = surfaceScale.setOutputScale (getScaleForSurfaceOutputs());
        // Refreshing here avoids cursor-specific change tracking in WaylandSurfaceScale::Update.
        Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
        handleScaleUpdate (update);
    }

    void handleScaleUpdate (const WaylandSurfaceScale::Update& update)
    {
        // Queue a full repaint before notifying listeners because a listener may destroy this peer.
        // This also ensures a full repaint when a scale change leaves the buffer dimensions unchanged.
        if (update.bufferGeometryChanged)
        {
            updateToplevelSizeConstraints();
            repaint (logicalBounds.withZeroOrigin());
        }

        if (update.scaleFactorToReport.has_value())
            scaleFactorListeners.call ([&] (ScaleFactorListener& l) { l.nativeScaleFactorChanged (*update.scaleFactorToReport); });
    }

    void createScaleObjects()
    {
        scaleObjects.reset();

        auto* windowSystem = WaylandWindowSystem::getInstance();
        auto* viewporterGlobal = windowSystem->getViewporter();

        // wp_viewporter also serves host scale overrides when the fractional scale manager is unavailable.
        if (viewporterGlobal == nullptr)
            return;

        ViewportHandle viewport { WaylandProtocol::wpViewporterGetViewport (viewporterGlobal, surface.get()) };

        if (viewport == nullptr)
            return;

        FractionalScaleHandle fractionalScale;
        auto* scaleManager = windowSystem->getFractionalScaleManager();

        if (scaleManager != nullptr)
        {
            fractionalScale.reset (WaylandProtocol::wpFractionalScaleManagerV1GetFractionalScale (scaleManager,
                                                                                                  surface.get()));

            if (fractionalScale != nullptr)
                WaylandProtocol::wpFractionalScaleV1AddListener (fractionalScale.get(),
                                                                 &fractionalScaleListener,
                                                                 callbackState.get());
        }

        scaleObjects.emplace (std::move (viewport), std::move (fractionalScale));
    }

    bool requestFrameCallback()
    {
        if (frameCallback != nullptr)
            return true;

        frameCallback.reset (WaylandProtocol::wlSurfaceFrame (surface.get()));

        if (frameCallback != nullptr)
            WaylandProtocol::wlCallbackAddListener (frameCallback.get(), &frameListener, callbackState.get());

        return frameCallback != nullptr;
    }

    void requestIdleFrameCallbackForVBlankListeners()
    {
        // A frame callback committed before the first buffer may never fire because the surface
        // is not yet mapped, which would block the initial repaint.
        if (! configured || ! visible || suspended || ! hasCommittedBuffer || surface == nullptr)
            return;

        if (frameCallback != nullptr || vBlankListeners.isEmpty())
            return;

        // A queued repaint requests the frame callback in its own commit.
        if (repainter != nullptr && repainter->hasPendingRepaints())
            return;

        if (! requestFrameCallback())
            return;

        // A frame callback request takes effect on the next commit.
        ++diagnostics.frameCallbackOnlyCommits;
        WaylandProtocol::wlSurfaceCommit (surface.get());
        WaylandWindowSystem::getInstance()->flush();
    }

    void vBlankListenerPresenceChanged() override
    {
        requestIdleFrameCallbackForVBlankListeners();
    }

    //==============================================================================
    Point<int> prepareToplevelConfigure (const WaylandToplevel::ConfigureInfo& info) override
    {
        const auto configuredSize = info.contentSize.value_or (Point<int>{});

        ++diagnostics.configuresReceived;
        diagnostics.lastConfigureWidth = configuredSize.x;
        diagnostics.lastConfigureHeight = configuredSize.y;
        diagnostics.firstConfigureReceived = true;
        diagnostics.lastConfigureActivated = info.activated;
        diagnostics.lastConfigureFullScreen = info.fullScreen;
        diagnostics.lastConfigureSuspended = info.suspended;

        fullScreenState.configureReceived (info.fullScreen);
        configured = true;

        // xdg-shell has no unminimise event, so an activated configure means the compositor showed us again.
        if (info.activated)
            wantsMinimised = false;

        const auto wasSuspended = std::exchange (suspended, info.suspended);

        // A suspended surface is not presented, so a pending frame callback may never arrive and
        // would block later repaints. Reset again when leaving suspension in case a repaint
        // committed while suspended requested another callback.
        if (suspended || wasSuspended)
            frameCallback.reset();

        // A JUCE host override makes the pending-size conversion depend on the current output scale.
        configureScaleUpdate = surfaceScale.setOutputScale (getScaleForSurfaceOutputs());

        if (info.contentSize.has_value())
        {
            const auto mappingMethod = getBufferMappingMethod();
            logicalBounds = logicalBounds.withSize (surfaceScale.convertSurfaceExtentToLogical (info.contentSize->x,
                                                                                                mappingMethod),
                                                    surfaceScale.convertSurfaceExtentToLogical (info.contentSize->y,
                                                                                                mappingMethod));
        }

        updateToplevelSizeConstraints();

        return getSurfaceContentSize();
    }

    void finishToplevelConfigure() override
    {
        const auto scaleUpdate = std::exchange (configureScaleUpdate, std::nullopt);

        if (scaleUpdate.has_value() && scaleUpdate->bufferGeometryChanged)
        {
            // A scale-factor listener may destroy this peer.
            const WeakReference<WaylandComponentPeer> deletionChecker { this };
            handleScaleUpdate (*scaleUpdate);

            if (deletionChecker == nullptr)
                return;
        }
        else
        {
            repaint (logicalBounds.withZeroOrigin());
        }

        handleMovedOrResized();
    }

    void toplevelCloseRequested() override
    {
        // User code can destroy the window inside handleUserClosingWindow, which would free
        // the libdecor frame while libdecor is still dispatching the close button's pointer event.
        MessageManager::callAsync ([safeComponent = Component::SafePointer<Component> { &getComponent() }]
        {
            if (safeComponent == nullptr)
                return;

            if (auto* peer = safeComponent->getPeer())
                peer->handleUserClosingWindow();
        });
    }

    void popupConfigured (Rectangle<int> parentRelativeBounds) override
    {
        if (auto* popupRole = getPopupRole())
        {
            configured = true;
            ++diagnostics.popupConfigures;
            logicalBounds = convertWaylandPopupConfigureToLogicalBounds (parentRelativeBounds,
                                                                         popupRole->parent.parentCoordinates);

            repaint (logicalBounds.withZeroOrigin());
            handleMovedOrResized();
        }
    }

    void popupDismissed() override
    {
        ++diagnostics.popupDoneEvents;

        // The compositor sends popup_done to each popup it dismisses, so each peer only dismisses its own component.
        dismissWaylandPopup (component);
    }

    void handleSurfaceEnter (wl_output* output)
    {
        surfaceOutputs.add (output);
        updateOutputScale();
    }

    void handleSurfaceLeave (wl_output* output)
    {
        surfaceOutputs.remove (output);
        updateOutputScale();
    }

    void handlePreferredBufferScale (int32_t scale)
    {
        const auto update = surfaceScale.setPreferredBufferScale (scale);
        // Refreshing here avoids cursor-specific change tracking in WaylandSurfaceScale::Update.
        Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
        handleScaleUpdate (update);
    }

    void handlePreferredFractionalScale (uint32_t scale120)
    {
        diagnostics.lastPreferredScale120 = (int) scale120;
        handleScaleUpdate (surfaceScale.setPreferredFractionalScale120 ((int) scale120));
    }

    void handleActivationTokenDone (const char* token)
    {
        auto* windowSystem = WaylandWindowSystem::getInstance();

        // The compositor may grant focus before it returns the activation token.
        if (auto* activation = windowSystem->getXdgActivation(); activation != nullptr && ! focused)
        {
            WaylandProtocol::xdgActivationV1Activate (activation, token, surface.get());
            windowSystem->flush();
        }

        pendingActivationToken.reset();
    }

    void handleFrameDone (wl_callback* callback)
    {
        if (callback != frameCallback.get())
            return;

        frameCallback.reset();
        diagnostics.frameCallbackFired = true;
        ++diagnostics.frameCallbacksReceived;

        // VBlank listeners and component painting can both destroy this peer.
        const WeakReference<WaylandComponentPeer> deletionChecker { this };

        callVBlankListeners (Time::getMillisecondCounterHiRes() * 0.001);

        if (deletionChecker == nullptr)
            return;

        if (repainter != nullptr)
            repainter->performAnyPendingRepaintsNow();

        if (deletionChecker == nullptr)
            return;

        // If nothing committed above, keep the vblank stream going
        requestIdleFrameCallbackForVBlankListeners();
    }

    //==============================================================================
    void keyboardFocusGained() override
    {
        if (! focused)
        {
            focused = true;
            handleFocusGain();
        }
    }

    void keyboardFocusLost() override
    {
        if (focused)
        {
            focused = false;
            handleFocusLoss();
        }
    }

    void modifierKeysChanged() override                     { handleModifierKeysChange(); }
    void keyStateChanged (bool isDown) override             { handleKeyUpOrDown (isDown); }
    void keyPressed (int keyCode, juce_wchar character) override    { handleKeyPress (keyCode, character); }

    bool ignoresKeyPresses() const override
    {
        return (getStyleFlags() & windowIgnoresKeyPresses) != 0;
    }

    wl_surface* getKeyEventFallbackSurface() const override
    {
        auto* popupRole = getPopupRole();
        return popupRole != nullptr && popupRole->popup != nullptr ? popupRole->parent.parentSurface
                                                                   : nullptr;
    }

    void pointerMoved (Point<float> position, int64 time) override
    {
        handleMouseEvent (MouseInputSource::InputSourceType::mouse, convertSurfacePointToLogical (position),
                          ModifierKeys::getCurrentModifiers(), MouseInputSource::defaultPressure,
                          MouseInputSource::defaultOrientation, time);
    }

    void pointerEntered() override
    {
        Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
    }

    Point<float> convertPointerPositionToGlobal (Point<float> position) override
    {
        return localToGlobal (convertSurfacePointToLogical (position));
    }

    void pointerButton (Point<float> position, bool pressed, int64 time) override
    {
        if (pressed)
        {
            toFront (true);

            // Application code run by toFront() may destroy this peer.
            if (! isValidPeer (this))
                return;
        }

        handleMouseEvent (MouseInputSource::InputSourceType::mouse, convertSurfacePointToLogical (position),
                          ModifierKeys::getCurrentModifiers(), MouseInputSource::defaultPressure,
                          MouseInputSource::defaultOrientation, time);
    }

    void pointerWheel (Point<float> position, const MouseWheelDetails& wheel, int64 time) override
    {
        handleMouseWheel (MouseInputSource::InputSourceType::mouse, convertSurfacePointToLogical (position), time, wheel);
    }

    bool dataDragMoved (const ComponentPeer::DragInfo& info) override
    {
        return handleDragMove (convertDataDragInfo (info));
    }

    void dataDragExited (const ComponentPeer::DragInfo& info) override
    {
        handleDragExit (convertDataDragInfo (info));
    }

    bool dataDropped (const ComponentPeer::DragInfo& info) override
    {
        return handleDragDrop (convertDataDragInfo (info));
    }

    int getPointerCursorScale() const override
    {
        return surfaceScale.getIntegerCompositorScale();
    }

    std::optional<WaylandPopupParentCandidate> getPopupParentCandidate() const override
    {
        if (auto* popupRole = getPopupRole())
        {
            // A hidden popup has no xdg_surface and its previous serial is no longer valid.
            if (popupRole->popup == nullptr)
                return std::nullopt;

            // Passive helpers stay outside the menu popup chain and are never parent candidates.
            if (popupRole->kind == WaylandPopupKind::passive)
                return std::nullopt;

            const auto mappedPopup = std::invoke ([&]() -> WaylandPopupParentCandidate::MappedPopup
            {
                if (const auto& grab = popupRole->grab)
                    return { grab->componentToDismiss.get(), grab->serial };

                return {};
            });

            return WaylandPopupParentCandidate { popupRole->popup->getXdgSurface(),
                                                 { logicalBounds,
                                                   surfaceScale.getLogicalToSurfaceScale (getBufferMappingMethod()) },
                                                 mappedPopup,
                                                 popupRole->parent.grabOwnerSurface };
        }

        auto* toplevel = getToplevel();

        if (toplevel == nullptr || toplevel->getXdgSurface() == nullptr)
            return std::nullopt;

        return WaylandPopupParentCandidate { toplevel->getXdgSurface(),
                                             { toplevel->getPopupParentBounds (logicalBounds),
                                               surfaceScale.getLogicalToSurfaceScale (getBufferMappingMethod()) },
                                             std::nullopt,
                                             surface.get() };
    }

    void popupGrabStarted (Component& componentToDismissIn) override
    {
        if (auto* toplevel = getToplevel())
            toplevel->popupGrabStarted (componentToDismissIn);
    }

    void popupGrabEnded() override
    {
        if (auto* toplevel = getToplevel())
            toplevel->popupGrabEnded();
    }

    void touchEvent (int touchIndex, Point<float> position, ModifierKeys mods, int64 time) override
    {
        handleMouseEvent (MouseInputSource::InputSourceType::touch, convertSurfacePointToLogical (position), mods,
                          MouseInputSource::defaultPressure, MouseInputSource::defaultOrientation,
                          time, {}, touchIndex);
    }

    WaylandSurfaceScale::BufferMappingMethod getBufferMappingMethod() const
    {
        return scaleObjects.has_value() ? WaylandSurfaceScale::BufferMappingMethod::viewport
                                        : WaylandSurfaceScale::BufferMappingMethod::integerBufferScale;
    }

    Point<float> convertSurfacePointToLogical (Point<float> position) const
    {
        return surfaceScale.convertSurfacePointToLogical (position,
                                                          logicalBounds.withZeroOrigin(),
                                                          getBufferMappingMethod());
    }

    ComponentPeer::DragInfo convertDataDragInfo (const ComponentPeer::DragInfo& info) const
    {
        auto result = info;
        result.position = convertSurfacePointToLogical (info.position.toFloat()).roundToInt();
        return result;
    }

    //==============================================================================
    void outputConfigurationChanged() override
    {
        updateOutputScale();
    }

    void outputWillBeDestroyed (wl_output* output) override
    {
        // A display hot-unplug is not guaranteed to produce wl_surface.leave, so stop tracking its wl_output here.
        surfaceOutputs.remove (output);
    }

    //==============================================================================
    static const wl_surface_listener surfaceListener;
    static const wl_callback_listener frameListener;
    static const wp_fractional_scale_v1_listener fractionalScaleListener;
    static const xdg_activation_token_v1_listener activationTokenListener;

    std::unique_ptr<WaylandRepaintManager> repainter;
    std::unique_ptr<WaylandPeerCallbackState> callbackState;

    std::unique_ptr<wl_surface, FunctionPointerDestructor<WaylandProtocol::wlSurfaceDestroy>> surface;
    std::variant<ToplevelRole, PopupRole> surfaceRole;
    std::unique_ptr<wl_callback, FunctionPointerDestructor<WaylandProtocol::wlCallbackDestroy>> frameCallback;
    std::unique_ptr<xdg_activation_token_v1, FunctionPointerDestructor<WaylandProtocol::xdgActivationTokenV1Destroy>> pendingActivationToken;
    std::optional<ScaleObjects> scaleObjects;

    Rectangle<int> logicalBounds;
    String windowTitle;
    WaylandFullScreenState fullScreenState;
    bool configured = false;
    bool visible = false;
    bool suspended = false;
    bool hasCommittedBuffer = false;
    bool wantsMinimised = false;
    bool focused = false;
    std::optional<WaylandSurfaceScale::Update> configureScaleUpdate;
    WaylandSurfaceScale surfaceScale;
    WaylandSurfaceOutputs surfaceOutputs;

    detail::WaylandPeerDiagnostics diagnostics;

    JUCE_DECLARE_WEAK_REFERENCEABLE (WaylandComponentPeer)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaylandComponentPeer)
};

const wl_surface_listener WaylandComponentPeer::surfaceListener
{
    [] (void* data, wl_surface*, wl_output* output)
    {
        if (auto peer = getPeerFromCallbackState (data))
            peer->handleSurfaceEnter (output);
    },
    [] (void* data, wl_surface*, wl_output* output)
    {
        if (auto peer = getPeerFromCallbackState (data))
            peer->handleSurfaceLeave (output);
    },
    [] (void* data, wl_surface*, int32_t scale)
    {
        if (auto peer = getPeerFromCallbackState (data))
            peer->handlePreferredBufferScale (scale);
    },
    // Rotated output presentation is not implemented, so ignore preferred_buffer_transform
    // (SUD-220).
    [] (void*, wl_surface*, uint32_t) {}
};

const wl_callback_listener WaylandComponentPeer::frameListener
{
    [] (void* data, wl_callback* callback, uint32_t)
    {
        if (auto peer = getPeerFromCallbackState (data))
            peer->handleFrameDone (callback);
    }
};

const wp_fractional_scale_v1_listener WaylandComponentPeer::fractionalScaleListener
{
    [] (void* data, wp_fractional_scale_v1*, uint32_t scale120)
    {
        if (auto peer = getPeerFromCallbackState (data))
            peer->handlePreferredFractionalScale (scale120);
    }
};

const xdg_activation_token_v1_listener WaylandComponentPeer::activationTokenListener
{
    [] (void* data, xdg_activation_token_v1*, const char* token)
    {
        if (auto peer = getPeerFromCallbackState (data))
            peer->handleActivationTokenDone (token);
    }
};

// The manager owns its buffers and destroys them in its destructor. That turns their
// proxies into zombies, so this listener can never fire on a dead manager.
const wl_buffer_listener WaylandComponentPeer::WaylandRepaintManager::bufferReleaseListener
{
    [] (void* data, wl_buffer* buffer)
    {
        static_cast<WaylandRepaintManager*> (data)->handleBufferRelease (buffer);
    }
};

const wl_callback_listener WaylandComponentPeer::OpenGLSurface::frameListener
{
    [] (void* data, wl_callback* callback, uint32_t)
    {
        static_cast<OpenGLSurface*> (data)->handleFrameDone (callback);
    }
};

std::unique_ptr<WaylandOpenGLSurface> WaylandComponentPeer::createOpenGLSurface (Component& target)
{
    return OpenGLSurface::create (*this, target);
}

ComponentPeer* createWaylandComponentPeer (Component& component, int styleFlags)
{
    return new WaylandComponentPeer (component, styleFlags);
}

bool isWaylandComponentPeer (const ComponentPeer* peer)
{
    return dynamic_cast<const WaylandComponentPeer*> (peer) != nullptr;
}

std::unique_ptr<WaylandOpenGLSurface> createWaylandOpenGLSurface (Component& component)
{
    if (auto* peer = dynamic_cast<WaylandComponentPeer*> (component.getPeer()))
        return peer->createOpenGLSurface (component);

    return {};
}

#if JUCE_WAYLAND_PEER_DIAGNOSTICS
std::optional<detail::WaylandPeerDiagnostics> detail::getWaylandPeerDiagnostics (ComponentPeer* peer)
{
    if (auto* waylandPeer = dynamic_cast<WaylandComponentPeer*> (peer))
        return waylandPeer->getDiagnostics();

    return {};
}
#endif

//==============================================================================
//==============================================================================
#if JUCE_UNIT_TESTS

class WaylandSizeConstraintsTests final : public UnitTest
{
public:
    WaylandSizeConstraintsTests()
        : UnitTest ("WaylandSizeConstraints", UnitTestCategories::gui) {}

    void runTest() override
    {
        testCase ("A bordered constrainer adds its border to wrapped size limits", [&]
        {
            ComponentBoundsConstrainer wrapped;
            wrapped.setSizeLimits (400, 200, 1024, 700);

            const BorderedConstrainer bordered { wrapped, { 10, 20, 30, 40 } };
            const auto constraints = getWaylandSizeConstraints (bordered);

            expect (constraints.minimum == Point<int> { 460, 240 });
            expect (constraints.maximum == Point<int> { 1084, 740 });
        });
    }

private:
    class BorderedConstrainer final : public BorderedComponentBoundsConstrainer
    {
    public:
        BorderedConstrainer (ComponentBoundsConstrainer& wrappedIn, BorderSize<int> borderIn)
            : wrapped (wrappedIn), border (borderIn) {}

        ComponentBoundsConstrainer* getWrappedConstrainer() const override { return &wrapped; }
        BorderSize<int> getAdditionalBorder() const override               { return border; }

    private:
        ComponentBoundsConstrainer& wrapped;
        BorderSize<int> border;
    };
};

static WaylandSizeConstraintsTests waylandSizeConstraintsTests;

//==============================================================================
class ComputeRegionsToPaintTests final : public UnitTest
{
public:
    ComputeRegionsToPaintTests()
        : UnitTest ("computeRegionsToPaint", UnitTestCategories::gui) {}

    void runTest() override
    {
        const Rectangle<int> logicalBounds { 0, 0, 100, 100 };
        const Rectangle<int> topLeft { 0, 0, 10, 10 };
        const Rectangle<int> middle { 40, 40, 10, 10 };

        constexpr auto scratchImageWasRecreated = true;
        constexpr auto scratchImageWasKept = false;

        testCase ("A buffer with no stale regions paints only its dirty regions", [&]
        {
            const RectangleList<int> dirtyRegions { topLeft };
            const std::optional<RectangleList<int>> knownStaleRegions { RectangleList<int>{} };

            const auto regions = computeRegionsToPaint (dirtyRegions, knownStaleRegions,
                                                        scratchImageWasKept, logicalBounds);
            expect (regions.getBounds() == topLeft);
        });

        testCase ("A buffer paints both stale and dirty regions", [&]
        {
            const RectangleList<int> dirtyRegions { topLeft };
            const std::optional<RectangleList<int>> knownStaleRegions { middle };

            const auto regions = computeRegionsToPaint (dirtyRegions, knownStaleRegions,
                                                        scratchImageWasKept, logicalBounds);
            expect (regions.containsRectangle (topLeft));
            expect (regions.containsRectangle (middle));
            expect (regions.getBounds() == topLeft.getUnion (middle));
        });

        testCase ("Unknown buffer contents force a full paint even for a small dirty region", [&]
        {
            const RectangleList<int> dirtyRegions { topLeft };

            const auto regions = computeRegionsToPaint (dirtyRegions, std::nullopt,
                                                        scratchImageWasKept, logicalBounds);
            expect (regions.containsRectangle (logicalBounds));
            expect (regions.getBounds() == logicalBounds);
        });

        testCase ("Recreating the scratch image forces a full paint even when buffer contents are known", [&]
        {
            const RectangleList<int> dirtyRegions { topLeft };
            const std::optional<RectangleList<int>> knownStaleRegions { middle };

            const auto regions = computeRegionsToPaint (dirtyRegions, knownStaleRegions,
                                                        scratchImageWasRecreated, logicalBounds);
            expect (regions.containsRectangle (logicalBounds));
            expect (regions.getBounds() == logicalBounds);
        });
    }
};

static ComputeRegionsToPaintTests computeRegionsToPaintTests;

//==============================================================================
class ComputeRegionsToReportAsDamageTests final : public UnitTest
{
public:
    ComputeRegionsToReportAsDamageTests()
        : UnitTest ("computeRegionsToReportAsDamage", UnitTestCategories::gui) {}

    void runTest() override
    {
        const Rectangle<int> bufferBounds { 0, 0, 200, 100 };
        const Rectangle<int> topLeft { 0, 0, 10, 10 };
        const Rectangle<int> middle { 40, 40, 10, 10 };

        testCase ("Stale buffer regions do not expand compositor damage", [&]
        {
            const RectangleList<int> dirtyBufferRegions { topLeft };
            const std::optional<RectangleList<int>> knownStaleRegions { middle };

            const auto regions = computeRegionsToReportAsDamage (dirtyBufferRegions, knownStaleRegions, bufferBounds);
            expect (regions.getBounds() == topLeft);
        });

        testCase ("Unknown buffer contents make the peer report the full buffer as damaged", [&]
        {
            const RectangleList<int> dirtyBufferRegions { topLeft };

            const auto regions = computeRegionsToReportAsDamage (dirtyBufferRegions, std::nullopt, bufferBounds);
            expect (regions.getBounds() == bufferBounds);
        });
    }
};

static ComputeRegionsToReportAsDamageTests computeRegionsToReportAsDamageTests;

//==============================================================================
class WaylandSurfaceScaleTests final : public UnitTest
{
public:
    WaylandSurfaceScaleTests()
        : UnitTest ("WaylandSurfaceScale", UnitTestCategories::gui) {}

    void runTest() override
    {
        using BufferMappingMethod = WaylandSurfaceScale::BufferMappingMethod;

        // Compositor scale
        // Compositor-driven fractional scales require both wp_viewporter and
        // wp_fractional_scale_manager_v1.

        testCase ("A viewport maps the buffer onto the logical size", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (200, 100));
            expectEquals (geometry.bufferScale, 1);
            expect (geometry.viewportDestination == Rectangle<int> (100, 50));
        });

        testCase ("A preferred buffer scale wins over the output scale", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setPreferredBufferScale (3);
            expectEquals (state.getScaleFactor(), 3.0);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (300, 150));
            expectEquals (geometry.bufferScale, 1);
            expect (geometry.viewportDestination == Rectangle<int> (100, 50));
        });

        testCase ("A fractional preferred scale wins over a preferred buffer scale", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredBufferScale (2);
            state.setPreferredFractionalScale120 (180);
            expectEquals (state.getScaleFactor(), 1.5);

            const auto update = state.setPreferredBufferScale (3);
            expect (! update.scaleFactorToReport.has_value());
            expect (! update.bufferGeometryChanged);
            expectEquals (state.getScaleFactor(), 1.5);
        });

        testCase ("A fractional preferred scale in 120ths determines the buffer size", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (180);
            expectEquals (state.getScaleFactor(), 1.5);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (150, 75));
            expectEquals (geometry.bufferScale, 1);
            expect (geometry.viewportDestination == Rectangle<int> (100, 50));
        });

        testCase ("A whole-number preference from the fractional-scale protocol produces matching buffer dimensions", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (240);
            expectEquals (state.getScaleFactor(), 2.0);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (200, 100));
            expect (geometry.viewportDestination == Rectangle<int> (100, 50));
        });

        testCase ("Fractional buffer dimensions round down below halfway", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (175);

            expect (state.getBufferGeometry ({ 3, 3 }, BufferMappingMethod::viewport).bufferBounds
                    == Rectangle<int> (4, 4));
        });

        testCase ("Fractional buffer dimensions round halfway away from zero", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (180);

            expect (state.getBufferGeometry ({ 3, 3 }, BufferMappingMethod::viewport).bufferBounds
                    == Rectangle<int> (5, 5));
        });

        testCase ("Fractional buffer dimensions round up above halfway", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (185);

            expect (state.getBufferGeometry ({ 3, 3 }, BufferMappingMethod::viewport).bufferBounds
                    == Rectangle<int> (5, 5));
        });

        testCase ("A 1x preference from the fractional-scale protocol keeps the buffer at the logical size", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (120);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (100, 50));
            expect (geometry.viewportDestination == Rectangle<int> (100, 50));
        });

        testCase ("Empty logical bounds produce an empty buffer", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (180);

            expect (state.getBufferGeometry ({}, BufferMappingMethod::viewport).bufferBounds.isEmpty());
        });

        testCase ("Output scales outside the supported range are clamped", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (0);
            expectEquals (state.getScaleFactor(), 1.0);

            state.setOutputScale (std::numeric_limits<int>::max());
            expectEquals (state.getScaleFactor(), 16.0);
        });

        testCase ("A fractional preferred scale of zero is ignored", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (180);

            state.setPreferredFractionalScale120 (0);
            expectEquals (state.getScaleFactor(), 1.5);
        });

        testCase ("Fractional preferred scales above 16x are ignored", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (16 * 120);
            expectEquals (state.getScaleFactor(), 16.0);

            state.setPreferredFractionalScale120 (180);
            state.setPreferredFractionalScale120 (16 * 120 + 1);
            expectEquals (state.getScaleFactor(), 1.5);
        });

        testCase ("Preferred buffer scales outside the supported range are ignored", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredBufferScale (2);

            state.setPreferredBufferScale (0);
            expectEquals (state.getScaleFactor(), 2.0);

            state.setPreferredBufferScale (17);
            expectEquals (state.getScaleFactor(), 2.0);
        });

        // JUCE scale override.
        testCase ("Only changes to the JUCE scale factor are reported", [&]
        {
            WaylandSurfaceScale state;

            auto update = state.setOutputScale (2);
            expect (update.scaleFactorToReport == 2.0);

            update = state.setPreferredFractionalScale120 (240);
            expect (! update.scaleFactorToReport.has_value());

            update = state.setPreferredFractionalScale120 (180);
            expect (update.scaleFactorToReport == 1.5);

            update = state.setOutputScale (3);
            expect (! update.scaleFactorToReport.has_value());

            update = state.setOverrideScale (1.5);
            expect (! update.scaleFactorToReport.has_value());

            update = state.setPreferredFractionalScale120 (240);
            expect (! update.scaleFactorToReport.has_value());

            update = state.setOverrideScale ({});
            expect (update.scaleFactorToReport == 2.0);
        });

        testCase ("A compositor scale change updates geometry while an override keeps the JUCE scale unchanged", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            auto update = state.setPreferredFractionalScale120 (180);
            expect (! update.scaleFactorToReport.has_value());
            expect (update.bufferGeometryChanged);

            update = state.setPreferredFractionalScale120 (180);
            expect (! update.scaleFactorToReport.has_value());
            expect (! update.bufferGeometryChanged);
        });

        testCase ("Matching render and compositor scales leave surface coordinates unchanged", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);

            expectEquals (state.getLogicalToSurfaceScale (BufferMappingMethod::viewport), 1.0);
            expectEquals (state.getLogicalToSurfaceScale (BufferMappingMethod::integerBufferScale), 1.0);
        });

        testCase ("A host scale override converts logical coordinates to the compositor surface scale", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            expectEquals (state.getLogicalToSurfaceScale (BufferMappingMethod::viewport), 0.75);
            expectEquals (state.getLogicalToSurfaceScale (BufferMappingMethod::integerBufferScale), 0.75);
        });

        testCase ("The override scale wins over the fractional preferred scale until it is cleared", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (240);

            state.setOverrideScale (1.25);
            expectEquals (state.getScaleFactor(), 1.25);
            expect (state.getBufferGeometry ({ 100, 100 }, BufferMappingMethod::viewport).bufferBounds
                    == Rectangle<int> (125, 125));

            state.setOverrideScale ({});
            expectEquals (state.getScaleFactor(), 2.0);
            expect (state.getBufferGeometry ({ 100, 100 }, BufferMappingMethod::viewport).bufferBounds
                    == Rectangle<int> (200, 200));
        });

        testCase ("A viewport combines a fractional compositor scale with an override", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (150);
            state.setOverrideScale (1.5);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (150, 75));
            expectEquals (geometry.bufferScale, 1);
            expect (geometry.viewportDestination == Rectangle<int> (120, 60));
        });

        testCase ("A non-positive override clears the override instead of applying it", [&]
        {
            WaylandSurfaceScale state;
            state.setPreferredFractionalScale120 (240);
            state.setOverrideScale (1.5);
            expectEquals (state.getScaleFactor(), 1.5);

            state.setOverrideScale (-1.0);
            expectEquals (state.getScaleFactor(), 2.0);
            expect (! state.getOverrideScale().has_value());

            state.setOverrideScale (1.5);
            expectEquals (state.getScaleFactor(), 1.5);

            state.setOverrideScale (0.0);
            expectEquals (state.getScaleFactor(), 2.0);
            expect (! state.getOverrideScale().has_value());
        });

        testCase ("A viewport maps an overridden buffer using the compositor scale", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            const auto geometry = state.getBufferGeometry ({ 101, 3 }, BufferMappingMethod::viewport);
            expect (geometry.bufferBounds == Rectangle<int> (152, 5));
            expectEquals (geometry.bufferScale, 1);
            expect (geometry.viewportDestination == Rectangle<int> (76, 2));
        });

        testCase ("Surface coordinates follow the rounded viewport destination", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            const auto logical = state.convertSurfacePointToLogical ({ 76.0f, 2.0f },
                                                                     { 101, 3 },
                                                                     BufferMappingMethod::viewport);
            expect (logical == Point<float> (101.0f, 3.0f));
        });

        testCase ("Surface extents round halfway away from zero when converted to logical coordinates", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (3);
            state.setOverrideScale (2.0);

            expectEquals (state.convertSurfaceExtentToLogical (3, BufferMappingMethod::viewport), 5);
        });

        // No-viewport fallback
        // Every compositor tested advertises wp_viewporter, but the protocol makes it optional.
        // The following cases cover integer buffer mapping in the rare case viewport is unavailable.
        testCase ("Integer buffer mapping uses one buffer pixel per logical pixel at the default scale", [&]
        {
            WaylandSurfaceScale state;
            expectEquals (state.getScaleFactor(), 1.0);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::integerBufferScale);
            expect (geometry.bufferBounds == Rectangle<int> (100, 50));
            expectEquals (geometry.bufferScale, 1);
            expect (! geometry.viewportDestination.has_value());
        });

        testCase ("Integer buffer mapping uses the output scale as the buffer scale", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            expectEquals (state.getScaleFactor(), 2.0);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::integerBufferScale);
            expect (geometry.bufferBounds == Rectangle<int> (200, 100));
            expectEquals (geometry.bufferScale, 2);
            expect (! geometry.viewportDestination.has_value());
        });

        testCase ("Integer buffer mapping uses the preferred buffer scale", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setPreferredBufferScale (3);
            expectEquals (state.getScaleFactor(), 3.0);

            const auto geometry = state.getBufferGeometry ({ 100, 50 }, BufferMappingMethod::integerBufferScale);
            expect (geometry.bufferBounds == Rectangle<int> (300, 150));
            expectEquals (geometry.bufferScale, 3);
            expect (! geometry.viewportDestination.has_value());
        });

        testCase ("A fractional override rounds buffer dimensions halfway away from zero", [&]
        {
            WaylandSurfaceScale state;
            state.setOverrideScale (1.5);

            const auto geometry = state.getBufferGeometry ({ 3, 3 }, BufferMappingMethod::integerBufferScale);
            expect (geometry.bufferBounds == Rectangle<int> (5, 5));
            expectEquals (geometry.bufferScale, 1);
        });

        testCase ("A fractional override uses the nearest buffer dimensions allowed by the output scale", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            const auto geometry = state.getBufferGeometry ({ 101, 3 }, BufferMappingMethod::integerBufferScale);
            expect (geometry.bufferBounds == Rectangle<int> (152, 4));
            expectEquals (geometry.bufferScale, 2);
            expectEquals (geometry.bufferBounds.getWidth() % geometry.bufferScale, 0);
            expectEquals (geometry.bufferBounds.getHeight() % geometry.bufferScale, 0);
            expect (! geometry.viewportDestination.has_value());
        });

        testCase ("Surface coordinates follow the rounded integer buffer geometry", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            const auto logical = state.convertSurfacePointToLogical ({ 76.0f, 2.0f },
                                                                     { 101, 3 },
                                                                     BufferMappingMethod::integerBufferScale);
            expect (logical == Point<float> (101.0f, 3.0f));
        });

        testCase ("Surface extents use the integer output scale when converted to logical coordinates", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (1.5);

            expectEquals (state.convertSurfaceExtentToLogical (76, BufferMappingMethod::integerBufferScale), 101);
        });

        testCase ("Fallback buffer dimensions round halfway away from zero to a valid multiple", [&]
        {
            WaylandSurfaceScale state;
            state.setOutputScale (2);
            state.setOverrideScale (2.5);

            const auto geometry = state.getBufferGeometry ({ 2, 2 }, BufferMappingMethod::integerBufferScale);
            expect (geometry.bufferBounds == Rectangle<int> (6, 6));
            expectEquals (geometry.bufferScale, 2);
        });

        // Map repainted areas from logical coordinates to buffer pixels.
        testCase ("A buffer the size of the logical bounds maps rectangles unchanged", [&]
        {
            const Rectangle<int> bounds { 100, 50 };
            const Rectangle<int> area { 10, 5, 20, 15 };

            expect (WaylandSurfaceScale::mapLogicalRectToBuffer (area, bounds, bounds) == area);
        });

        testCase ("A buffer at twice the logical size doubles rectangle positions and sizes", [&]
        {
            const auto mapped = WaylandSurfaceScale::mapLogicalRectToBuffer ({ 10, 5, 20, 10 },
                                                                             { 100, 50 },
                                                                             { 200, 100 });
            expect (mapped == Rectangle<int> (20, 10, 40, 20));
        });

        testCase ("A fractionally larger buffer rounds rectangle edges outwards", [&]
        {
            const auto mapped = WaylandSurfaceScale::mapLogicalRectToBuffer ({ 10, 10, 10, 10 },
                                                                             { 100, 100 },
                                                                             { 125, 125 });
            // The left and top edges fall inside pixel 12, and the right and bottom edges end at 25.
            expect (mapped == Rectangle<int> (12, 12, 13, 13));
        });

        testCase ("Logical rectangles that tile the surface map onto every buffer pixel", [&]
        {
            const Rectangle<int> logicalBounds { 100, 100 };
            const Rectangle<int> bufferBounds { 125, 125 };

            RectangleList<int> mapped;
            mapped.add (WaylandSurfaceScale::mapLogicalRectToBuffer ({ 0, 0, 50, 100 }, logicalBounds, bufferBounds));
            mapped.add (WaylandSurfaceScale::mapLogicalRectToBuffer ({ 50, 0, 50, 100 }, logicalBounds, bufferBounds));

            expect (mapped.containsRectangle (bufferBounds));
        });

        testCase ("A rounded buffer extent maps using the ratio between the buffer and logical sizes", [&]
        {
            // A logical width of 101 at a render scale of 1.25 rounds to a buffer width of 126.
            const Rectangle<int> logicalBounds { 101, 101 };
            const Rectangle<int> bufferBounds { 126, 126 };

            const auto atRightEdge = WaylandSurfaceScale::mapLogicalRectToBuffer ({ 91, 0, 10, 10 },
                                                                                  logicalBounds,
                                                                                  bufferBounds);
            expectEquals (atRightEdge.getRight(), 126);

            const auto nearLeftEdge = WaylandSurfaceScale::mapLogicalRectToBuffer ({ 20, 0, 4, 4 },
                                                                                   logicalBounds,
                                                                                   bufferBounds);
            // The render scale of 1.25 would place this edge on pixel 25 instead.
            expectEquals (nearLeftEdge.getX(), 24);
        });

        testCase ("An empty logical size maps every rectangle to an empty rectangle", [&]
        {
            expect (WaylandSurfaceScale::mapLogicalRectToBuffer ({ 10, 10, 10, 10 }, {}, { 125, 125 }).isEmpty());
        });

        testCase ("A child surface the size of its parent maps rectangles unchanged", [&]
        {
            const Rectangle<int> bounds { 100, 50 };
            const Rectangle<int> area { 10, 5, 20, 15 };

            expect (WaylandSurfaceScale::mapLogicalRectToSurface (area, bounds, bounds) == area);
        });

        testCase ("Adjacent child surfaces share one rounded fractional boundary", [&]
        {
            const Rectangle<int> logicalBounds { 2, 1 };
            const Rectangle<int> surfaceBounds { 3, 1 };
            const auto left = WaylandSurfaceScale::mapLogicalRectToSurface ({ 0, 0, 1, 1 },
                                                                            logicalBounds,
                                                                            surfaceBounds);
            const auto right = WaylandSurfaceScale::mapLogicalRectToSurface ({ 1, 0, 1, 1 },
                                                                             logicalBounds,
                                                                             surfaceBounds);

            expectEquals (left.getRight(), right.getX());
            expectEquals (left.getWidth() + right.getWidth(), surfaceBounds.getWidth());
        });
    }
};

static WaylandSurfaceScaleTests waylandSurfaceScaleTests;

//==============================================================================
class WaylandSurfaceOutputsTests final : public UnitTest
{
public:
    WaylandSurfaceOutputsTests()
        : UnitTest ("WaylandSurfaceOutputs", UnitTestCategories::gui) {}

    void runTest() override
    {
        // A wl_output is an opaque handle, so these tests only need two distinct addresses.
        int firstStorage = 0;
        int secondStorage = 0;
        auto* first = reinterpret_cast<wl_output*> (&firstStorage);
        auto* second = reinterpret_cast<wl_output*> (&secondStorage);

        const auto unknownScale = [] (wl_output*) { return std::optional<int>{}; };

        testCase ("A surface that is not on an output uses the fallback scale", [&]
        {
            WaylandSurfaceOutputs surfaceOutputs;

            expectEquals (surfaceOutputs.size(), 0);
            expectEquals (surfaceOutputs.getLargestScale (3, [] (wl_output*) { return std::optional<int> (1); }), 3);
        });

        testCase ("Repeated enter events track an output once", [&]
        {
            WaylandSurfaceOutputs surfaceOutputs;
            surfaceOutputs.add (first);
            surfaceOutputs.add (first);

            expectEquals (surfaceOutputs.size(), 1);
            expect (surfaceOutputs.contains (first));
        });

        testCase ("Removing an untracked output changes nothing", [&]
        {
            WaylandSurfaceOutputs surfaceOutputs;
            surfaceOutputs.add (first);
            surfaceOutputs.remove (second);

            expectEquals (surfaceOutputs.size(), 1);
            expect (surfaceOutputs.contains (first));
        });

        testCase ("A surface uses the largest scale of the outputs that contain it", [&]
        {
            WaylandSurfaceOutputs surfaceOutputs;
            surfaceOutputs.add (first);
            surfaceOutputs.add (second);

            const auto scaleForOutput = [&] (wl_output* output) { return std::optional<int> (output == second ? 3 : 2); };
            expectEquals (surfaceOutputs.getLargestScale (1, scaleForOutput), 3);

            surfaceOutputs.remove (second);
            expectEquals (surfaceOutputs.getLargestScale (1, scaleForOutput), 2);
        });

        testCase ("A surface ignores an output that has not reported its scale", [&]
        {
            WaylandSurfaceOutputs surfaceOutputs;
            surfaceOutputs.add (first);
            surfaceOutputs.add (second);

            const auto scaleForOutput = [&] (wl_output* output)
            {
                return output == second ? std::optional<int> (2) : std::nullopt;
            };

            expectEquals (surfaceOutputs.getLargestScale (4, scaleForOutput), 2);
        });

        testCase ("A surface uses the fallback when none of its outputs has reported a scale", [&]
        {
            WaylandSurfaceOutputs surfaceOutputs;
            surfaceOutputs.add (first);
            surfaceOutputs.add (second);

            expectEquals (surfaceOutputs.getLargestScale (2, unknownScale), 2);
        });
    }
};

static WaylandSurfaceOutputsTests waylandSurfaceOutputsTests;

#endif

} // namespace juce
