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

WaylandPopupPlacement makeWaylandPopupPlacement (Rectangle<int> popupBounds,
                                                 const WaylandPopupParentCoordinates& parentCoordinates)
{
    const auto parentRelativePosition = parentCoordinates.logicalToSurface (
        popupBounds.getPosition() - parentCoordinates.logicalBounds.getPosition());
    const auto parentSurfaceSize = parentCoordinates.getSurfaceSize();
    const Point<int> anchorPosition
    {
        jlimit (0, jmax (0, parentSurfaceSize.x - 1), parentRelativePosition.x),
        jlimit (0, jmax (0, parentSurfaceSize.y - 1), parentRelativePosition.y)
    };

    // Positions use the parent surface's coordinate system. Popup dimensions use
    // the popup surface's coordinate system and must not inherit the parent's scale.
    return
    {
        { anchorPosition.x, anchorPosition.y, 1, 1 },
        { jmax (1, popupBounds.getWidth()), jmax (1, popupBounds.getHeight()) },
        parentRelativePosition - anchorPosition
    };
}

Rectangle<int> convertWaylandPopupConfigureToLogicalBounds (
    Rectangle<int> parentRelativeSurfaceBounds,
    const WaylandPopupParentCoordinates& parentCoordinates)
{
    return parentRelativeSurfaceBounds.withPosition (
        parentCoordinates.surfaceToLogical (parentRelativeSurfaceBounds.getPosition())
            + parentCoordinates.logicalBounds.getPosition());
}

using XdgPositionerHandle = std::unique_ptr<xdg_positioner,
                                            FunctionPointerDestructor<WaylandProtocol::xdgPositionerDestroy>>;

static XdgPositionerHandle createPositioner (xdg_wm_base& wmBase, const WaylandPopupPlacement& placement)
{
    XdgPositionerHandle positioner { WaylandProtocol::xdgWmBaseCreatePositioner (&wmBase) };

    if (positioner == nullptr)
        return {};

    const auto anchor = placement.anchorRectangle;
    WaylandProtocol::xdgPositionerSetSize (positioner.get(), placement.popupSize.x, placement.popupSize.y);
    WaylandProtocol::xdgPositionerSetAnchorRect (positioner.get(), anchor.getX(), anchor.getY(),
                                                 anchor.getWidth(), anchor.getHeight());
    WaylandProtocol::xdgPositionerSetAnchor (positioner.get(), placement.anchor);
    WaylandProtocol::xdgPositionerSetGravity (positioner.get(), placement.gravity);
    WaylandProtocol::xdgPositionerSetConstraintAdjustment (positioner.get(), placement.constraintAdjustment);
    WaylandProtocol::xdgPositionerSetOffset (positioner.get(), placement.offset.x, placement.offset.y);
    return positioner;
}

static Rectangle<int> getRequestedParentRelativeBounds (const WaylandPopupPlacement& placement)
{
    const auto position = placement.anchorRectangle.getPosition() + placement.offset;
    return { position.x, position.y, placement.popupSize.x, placement.popupSize.y };
}

std::optional<WaylandPopupKind> getWaylandPopupKind (int styleFlags)
{
    if ((styleFlags & ComponentPeer::windowIsTemporary) == 0)
        return std::nullopt;

    return (styleFlags & ComponentPeer::windowIgnoresMouseClicks) != 0 ? WaylandPopupKind::passive
                                                                       : WaylandPopupKind::interactive;
}

std::optional<WaylandPopupParent> findWaylandPopupParent (const WaylandPopupParentContext& context,
                                                          WaylandPopupKind kind)
{
    const auto makeParent = [] (const WaylandPopupParentContext::Surface& surface,
                                const WaylandPopupParentCandidate& candidate,
                                std::shared_ptr<WaylandPopupGrabSerial> grabSerial)
    {
        return WaylandPopupParent { surface.surface,
                                    candidate.parentXdgSurface,
                                    candidate.parentCoordinates,
                                    std::move (grabSerial),
                                    candidate.mappedPopup.has_value()
                                        ? candidate.mappedPopup->componentToDismiss
                                        : nullptr,
                                    candidate.grabOwnerSurface };
    };

    const auto findSurface = [&context] (wl_surface* surface) -> const WaylandPopupParentContext::Surface*
    {
        if (surface == nullptr)
            return nullptr;

        const auto it = std::find_if (context.surfaces.begin(), context.surfaces.end(),
                                      [surface] (const auto& entry) { return entry.surface == surface; });
        return it != context.surfaces.end() ? &*it : nullptr;
    };

    // Keep interactive popups in their existing parent chain even when an ungrabbed menu
    // leaves keyboard focus on its toplevel. Peers register in creation order, so the
    // reverse scan starts at the topmost popup.
    if (kind == WaylandPopupKind::interactive)
    {
        for (auto it = context.surfaces.rbegin(); it != context.surfaces.rend(); ++it)
        {
            if (it->listener == nullptr)
                continue;

            const auto candidate = it->listener->getPopupParentCandidate();

            if (! candidate.has_value() || ! candidate->mappedPopup.has_value())
                continue;

            // Weston rejects an explicit grab whose parent popup has no explicit grab.
            // Prefer a fresh trigger serial, otherwise reuse the parent's grab serial.
            auto grabSerial = std::invoke ([&]() -> std::shared_ptr<WaylandPopupGrabSerial>
            {
                if (! candidate->mappedPopup->grabSerial.has_value())
                    return nullptr;

                if (context.triggerSerial != nullptr)
                    return context.triggerSerial;

                return std::make_shared<WaylandPopupGrabSerial> (*candidate->mappedPopup->grabSerial);
            });

            return makeParent (*it, *candidate, std::move (grabSerial));
        }
    }

    const auto* input = findSurface (context.inputSurface);

    if (input == nullptr || input->listener == nullptr)
        return std::nullopt;

    const auto candidate = input->listener->getPopupParentCandidate();

    if (! candidate.has_value())
        return std::nullopt;

    // Passive helpers use the popup chain's toplevel rather than joining the chain.
    if (kind == WaylandPopupKind::passive && candidate->mappedPopup.has_value())
    {
        const auto* toplevel = findSurface (candidate->grabOwnerSurface);

        if (toplevel == nullptr || toplevel->listener == nullptr)
            return std::nullopt;

        if (const auto toplevelCandidate = toplevel->listener->getPopupParentCandidate())
        {
            // A failure means the popup chain did not retain its original toplevel.
            jassert (! toplevelCandidate->mappedPopup.has_value());

            if (! toplevelCandidate->mappedPopup.has_value())
                return makeParent (*toplevel, *toplevelCandidate, nullptr);
        }

        return std::nullopt;
    }

    return makeParent (*input, *candidate,
                       kind == WaylandPopupKind::interactive ? context.triggerSerial : nullptr);
}

void dismissWaylandPopup (Component& componentToDismiss)
{
    const WeakReference<Component> deletionChecker { &componentToDismiss };
    componentToDismiss.exitModalState (0);

    if (deletionChecker != nullptr)
        deletionChecker->setVisible (false);
}

std::unique_ptr<WaylandPopup> WaylandPopup::create (Delegate& delegate,
                                                    wl_surface& surface,
                                                    xdg_surface& parent,
                                                    const WaylandPopupPlacement& placement,
                                                    std::optional<uint32_t> grabSerial)
{
    auto result = std::unique_ptr<WaylandPopup> { new WaylandPopup (delegate,
                                                                    surface,
                                                                    parent,
                                                                    placement,
                                                                    grabSerial) };

    if (result->xdgPopup != nullptr)
        return result;

    return nullptr;
}

WaylandPopup::WaylandPopup (Delegate& delegateIn,
                            wl_surface& surfaceIn,
                            xdg_surface& parent,
                            const WaylandPopupPlacement& placement,
                            std::optional<uint32_t> grabSerial)
    : delegate (delegateIn),
      surface (surfaceIn),
      requestedParentRelativeBounds (getRequestedParentRelativeBounds (placement))
{
    auto* windowSystem = WaylandWindowSystem::getInstance();
    auto* wmBase = windowSystem->getXdgWmBase();

    if (wmBase == nullptr)
        return;

    xdgSurface.reset (WaylandProtocol::xdgWmBaseGetXdgSurface (wmBase, &surface));

    if (xdgSurface == nullptr)
        return;

    const auto positioner = createPositioner (*wmBase, placement);

    if (positioner == nullptr)
        return;

    xdgPopup.reset (WaylandProtocol::xdgSurfaceGetPopup (xdgSurface.get(), &parent, positioner.get()));

    if (xdgPopup == nullptr)
        return;

    WaylandProtocol::xdgSurfaceAddListener (xdgSurface.get(), &surfaceListener, this);
    WaylandProtocol::xdgPopupAddListener (xdgPopup.get(), &popupListener, this);

    if (grabSerial.has_value())
        if (auto* seat = windowSystem->getSeat())
            WaylandProtocol::xdgPopupGrab (xdgPopup.get(), seat, *grabSerial);
}

void WaylandPopup::show()
{
    if (mapRequested)
        return;

    mapRequested = true;
    WaylandProtocol::wlSurfaceCommit (&surface);
    WaylandWindowSystem::getInstance()->flush();
}

bool WaylandPopup::reposition (const WaylandPopupPlacement& placement)
{
    if (! mapRequested || xdgPopup == nullptr)
        return false;

    auto* windowSystem = WaylandWindowSystem::getInstance();
    auto* wmBase = windowSystem->getXdgWmBase();

    if (wmBase == nullptr)
        return false;

    const auto positioner = createPositioner (*wmBase, placement);

    if (positioner == nullptr
        || ! WaylandProtocol::xdgPopupReposition (xdgPopup.get(), positioner.get(), nextRepositionToken))
    {
        return false;
    }

    ++nextRepositionToken;
    requestedParentRelativeBounds = getRequestedParentRelativeBounds (placement);
    windowSystem->flush();
    return true;
}

void WaylandPopup::handlePopupConfigure (Rectangle<int> parentRelativeBounds)
{
    pendingConfiguredBounds = parentRelativeBounds;
}

void WaylandPopup::handleSurfaceConfigure (uint32_t serial)
{
    const auto bounds = std::exchange (pendingConfiguredBounds, std::nullopt)
                            .value_or (requestedParentRelativeBounds);

    WaylandProtocol::xdgSurfaceAckConfigure (xdgSurface.get(), serial);

    delegate.popupConfigured (bounds);
}

const xdg_surface_listener WaylandPopup::surfaceListener
{
    [] (void* data, xdg_surface*, uint32_t serial)
    {
        static_cast<WaylandPopup*> (data)->handleSurfaceConfigure (serial);
    }
};

const xdg_popup_listener WaylandPopup::popupListener
{
    [] (void* data, xdg_popup*, int32_t x, int32_t y, int32_t width, int32_t height)
    {
        static_cast<WaylandPopup*> (data)->handlePopupConfigure ({ x, y, width, height });
    },
    [] (void* data, xdg_popup*)
    {
        static_cast<WaylandPopup*> (data)->delegate.popupDismissed();
    },
    // This event only echoes the reposition request token.
    // The next configure provides the chosen bounds and the serial.
    [] (void*, xdg_popup*, uint32_t) {}
};

#if JUCE_UNIT_TESTS

namespace
{
    struct TestPopupParentListener final : WaylandPopupParentListener
    {
        std::optional<WaylandPopupParentCandidate> getPopupParentCandidate() const override
        {
            return candidate;
        }

        void popupGrabStarted (Component&) override {}
        void popupGrabEnded() override {}

        std::optional<WaylandPopupParentCandidate> candidate;
    };

    template <typename Type>
    Type* fakePopupHandle (uintptr_t value)
    {
        return reinterpret_cast<Type*> (value);
    }
}

//==============================================================================
class WaylandPopupParentTests final : public UnitTest
{
public:
    WaylandPopupParentTests()
        : UnitTest ("WaylandPopupParent", UnitTestCategories::gui) {}

    void runTest() override
    {
        testCase ("The most recently registered mapped popup becomes the parent", [&]
        {
            Component firstComponentToDismiss;
            Component secondComponentToDismiss;
            TestPopupParentListener first;
            TestPopupParentListener second;
            auto* firstSurface = fakePopupHandle<wl_surface> (1);
            auto* secondSurface = fakePopupHandle<wl_surface> (2);
            auto* firstXdgSurface = fakePopupHandle<xdg_surface> (3);
            auto* secondXdgSurface = fakePopupHandle<xdg_surface> (4);
            first.candidate = WaylandPopupParentCandidate {
                firstXdgSurface,
                {},
                WaylandPopupParentCandidate::MappedPopup { &firstComponentToDismiss, std::nullopt },
                nullptr
            };
            second.candidate = WaylandPopupParentCandidate {
                secondXdgSurface,
                {},
                WaylandPopupParentCandidate::MappedPopup { &secondComponentToDismiss, std::nullopt },
                nullptr
            };

            WaylandPopupParentContext context;
            context.surfaces = { { firstSurface, &first }, { secondSurface, &second } };
            const auto parent = findWaylandPopupParent (context, WaylandPopupKind::interactive);

            expect (parent.has_value());

            if (! parent.has_value())
                return;

            expect (parent->parentSurface == secondSurface);
            expect (parent->parentXdgSurface == secondXdgSurface);
            expect (parent->componentToDismiss == &secondComponentToDismiss);
        });

        testCase ("Destroying the component to dismiss does not remove a mapped popup from parent selection", [&]
        {
            auto component = std::make_unique<Component>();
            WeakReference<Component> componentToDismiss { component.get() };
            expect (componentToDismiss != nullptr);
            TestPopupParentListener listener;
            auto* surface = fakePopupHandle<wl_surface> (1);
            auto* xdgSurface = fakePopupHandle<xdg_surface> (2);
            component.reset();
            expect (componentToDismiss == nullptr);
            listener.candidate = WaylandPopupParentCandidate {
                xdgSurface,
                {},
                WaylandPopupParentCandidate::MappedPopup { componentToDismiss.get(), std::nullopt },
                nullptr
            };

            WaylandPopupParentContext context;
            context.surfaces = { { surface, &listener } };
            const auto parent = findWaylandPopupParent (context, WaylandPopupKind::interactive);

            expect (parent.has_value());

            if (! parent.has_value())
                return;

            expect (parent->parentSurface == surface);
            expect (parent->parentXdgSurface == xdgSurface);
            expect (parent->componentToDismiss == nullptr);
        });

        testCase ("A child reuses its parent's explicit-grab serial when no trigger is available", [&]
        {
            Component componentToDismiss;
            TestPopupParentListener listener;
            auto* surface = fakePopupHandle<wl_surface> (1);
            listener.candidate = WaylandPopupParentCandidate {
                fakePopupHandle<xdg_surface> (2),
                {},
                WaylandPopupParentCandidate::MappedPopup { &componentToDismiss, 42 },
                nullptr
            };

            WaylandPopupParentContext context;
            context.surfaces = { { surface, &listener } };
            const auto parent = findWaylandPopupParent (context, WaylandPopupKind::interactive);

            expect (parent.has_value());

            if (! parent.has_value())
                return;

            expect (parent->grabSerial != nullptr);

            if (parent->grabSerial != nullptr)
                expect (parent->grabSerial->claim() == std::optional<uint32_t> { 42 });
        });

        testCase ("A child of an ungrabbed popup has no grab serial", [&]
        {
            Component componentToDismiss;
            TestPopupParentListener listener;
            listener.candidate = WaylandPopupParentCandidate {
                fakePopupHandle<xdg_surface> (2),
                {},
                WaylandPopupParentCandidate::MappedPopup { &componentToDismiss, std::nullopt },
                nullptr
            };

            WaylandPopupParentContext context;
            context.surfaces = { { fakePopupHandle<wl_surface> (1), &listener } };
            const auto parent = findWaylandPopupParent (context, WaylandPopupKind::interactive);

            expect (parent.has_value());

            if (! parent.has_value())
                return;

            expect (parent->grabSerial == nullptr);
        });

        testCase ("A submenu ignores a newer tooltip when choosing its parent", [&]
        {
            Component componentToDismiss;
            TestPopupParentListener menuListener;
            TestPopupParentListener tooltipListener;
            auto* menuSurface = fakePopupHandle<wl_surface> (1);
            auto* menuXdgSurface = fakePopupHandle<xdg_surface> (2);
            auto* tooltipSurface = fakePopupHandle<wl_surface> (3);

            // The mapped menu is available as the submenu's parent.
            menuListener.candidate = WaylandPopupParentCandidate {
                menuXdgSurface,
                {},
                WaylandPopupParentCandidate::MappedPopup { &componentToDismiss, std::nullopt },
                nullptr
            };

            // The newer tooltip offers no parent candidate.
            WaylandPopupParentContext context;
            context.surfaces = { { menuSurface, &menuListener }, { tooltipSurface, &tooltipListener } };

            // Parent selection ignores the tooltip and finds the menu.
            const auto parent = findWaylandPopupParent (context, WaylandPopupKind::interactive);

            expect (parent.has_value());

            if (! parent.has_value())
                return;

            expect (parent->parentSurface == menuSurface);
            expect (parent->parentXdgSurface == menuXdgSurface);
        });

        testCase ("A tooltip shown over a menu is parented to the window", [&]
        {
            Component componentToDismiss;
            TestPopupParentListener windowListener;
            TestPopupParentListener menuListener;
            auto* windowSurface = fakePopupHandle<wl_surface> (1);
            auto* windowXdgSurface = fakePopupHandle<xdg_surface> (2);
            auto* menuSurface = fakePopupHandle<wl_surface> (3);
            windowListener.candidate = WaylandPopupParentCandidate {
                windowXdgSurface,
                {},
                std::nullopt,
                windowSurface
            };
            menuListener.candidate = WaylandPopupParentCandidate {
                fakePopupHandle<xdg_surface> (4),
                {},
                WaylandPopupParentCandidate::MappedPopup { &componentToDismiss, 42 },
                windowSurface
            };

            WaylandPopupParentContext context;
            context.surfaces = { { windowSurface, &windowListener }, { menuSurface, &menuListener } };
            context.inputSurface = menuSurface;
            const auto parent = findWaylandPopupParent (context, WaylandPopupKind::passive);

            expect (parent.has_value());

            if (! parent.has_value())
                return;

            expect (parent->parentSurface == windowSurface);
            expect (parent->parentXdgSurface == windowXdgSurface);
            expect (parent->componentToDismiss == nullptr);
            expect (parent->grabSerial == nullptr);
        });
    }
};

static WaylandPopupParentTests waylandPopupParentTests;

//==============================================================================
class WaylandPopupPlacementTests final : public UnitTest
{
public:
    WaylandPopupPlacementTests()
        : UnitTest ("WaylandPopupPlacement", UnitTestCategories::gui) {}

    void runTest() override
    {
        testCase ("Requested popup bounds determine a parent-relative anchor, size, and offset", [&]
        {
            const auto placement = makeWaylandPopupPlacement ({ 130, 90, 200, 100 },
                                                              { { 100, 50, 400, 300 }, 1.0 });

            expect (placement.anchorRectangle == Rectangle<int> (30, 40, 1, 1));
            expect (placement.popupSize == Point<int> (200, 100));
            expect (placement.offset.isOrigin());
            expectEquals ((int) placement.anchor, (int) WaylandProtocol::xdgPositionerAnchorTopLeft);
            expectEquals ((int) placement.gravity, (int) WaylandProtocol::xdgPositionerGravityBottomRight);
        });

        testCase ("Empty popup dimensions are raised to the protocol minimum", [&]
        {
            const auto placement = makeWaylandPopupPlacement ({ 10, 20, 0, -1 }, {});

            expect (placement.popupSize == Point<int> (1, 1));
        });

        testCase ("Requested positions above and to the left of the parent use the first valid anchor plus an offset", [&]
        {
            const auto placement = makeWaylandPopupPlacement ({ 80, 40, 200, 100 },
                                                              { { 100, 50, 400, 300 }, 1.0 });

            expect (placement.anchorRectangle == Rectangle<int> (0, 0, 1, 1));
            expect (placement.offset == Point<int> (-20, -10));
        });

        testCase ("Requested positions below and to the right of the parent use the last valid anchor plus an offset", [&]
        {
            const auto placement = makeWaylandPopupPlacement ({ 650, 410, 200, 100 },
                                                              { { 100, 50, 400, 300 }, 1.0 });

            expect (placement.anchorRectangle == Rectangle<int> (399, 299, 1, 1));
            expect (placement.offset == Point<int> (151, 61));
        });

        testCase ("An anchor at the parent edge and its offset recover the requested parent-relative bounds", [&]
        {
            const WaylandPopupParentCoordinates parentCoordinates { { 100, 50, 400, 300 }, 1.0 };
            const Rectangle<int> popupBounds { 650, 410, 240, 120 };
            const auto placement = makeWaylandPopupPlacement (popupBounds, parentCoordinates);
            const auto requestedBounds = getRequestedParentRelativeBounds (placement);
            const auto expectedBounds = popupBounds.translated (-parentCoordinates.logicalBounds.getX(),
                                                                 -parentCoordinates.logicalBounds.getY());

            expect (requestedBounds == expectedBounds,
                    "Expected " + expectedBounds.toString() + ", got " + requestedBounds.toString());
        });

        testCase ("A host scale override converts parent-relative popup positions to surface coordinates", [&]
        {
            const WaylandPopupParentCoordinates parentCoordinates { { 30, 60, 481, 319 }, 0.75 };
            const auto placement = makeWaylandPopupPlacement ({ 509, 219, 125, 20 }, parentCoordinates);

            expect (placement.anchorRectangle == Rectangle<int> (359, 119, 1, 1));
            expect (placement.popupSize == Point<int> (125, 20));
            expect (placement.offset.isOrigin());
        });

        testCase ("Popup configure positions are converted from parent surface coordinates to logical coordinates", [&]
        {
            const WaylandPopupParentCoordinates parentCoordinates { { 30, 60, 481, 319 }, 0.75 };
            const auto bounds = convertWaylandPopupConfigureToLogicalBounds ({ 359, 119, 125, 20 },
                                                                             parentCoordinates);

            expect (bounds == Rectangle<int> (509, 219, 125, 20));
        });

        testCase ("Temporary window flags select the popup kind", [&]
        {
            constexpr auto popupMenuFlags = ComponentPeer::windowIsTemporary
                                          | ComponentPeer::windowIgnoresKeyPresses
                                          | ComponentPeer::windowHasDropShadow;
            constexpr auto tooltipFlags = popupMenuFlags | ComponentPeer::windowIgnoresMouseClicks;
            constexpr auto dragImageFlags = ComponentPeer::windowIsTemporary
                                          | ComponentPeer::windowIgnoresMouseClicks;

            expect (getWaylandPopupKind (popupMenuFlags) == WaylandPopupKind::interactive);
            expect (getWaylandPopupKind (tooltipFlags) == WaylandPopupKind::passive);
            expect (getWaylandPopupKind (dragImageFlags) == WaylandPopupKind::passive);
            expect (! getWaylandPopupKind (ComponentPeer::windowAppearsOnTaskbar).has_value());
        });
    }
};

static WaylandPopupPlacementTests waylandPopupPlacementTests;

#endif

} // namespace juce
