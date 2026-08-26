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

// Request wrappers and wl_interface tables are written here so JUCE can load libwayland
// at runtime without Wayland development headers. This mirrors how JUCE handles X11Symbols.
//
// Wayland protocols are append-only. New requests only ever get higher
// numbers. Opcodes and argument signatures:
//   https://gitlab.freedesktop.org/wayland/wayland/-/blob/main/protocol/wayland.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/xdg-shell/xdg-shell.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/xdg-output/xdg-output-unstable-v1.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/viewporter/viewporter.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/fractional-scale/fractional-scale-v1.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/alpha-modifier/alpha-modifier-v1.xml
//   https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/xdg-activation/xdg-activation-v1.xml
namespace juce::WaylandProtocol
{

namespace
{
    constexpr uint32_t destroyFlag = WL_MARSHAL_FLAG_DESTROY;

    // Request opcodes in protocol declaration order
    // Append-only. Never reorder these, only add at the end.
    enum class WlDisplayRequest           : uint32_t { sync, getRegistry };
    enum class WlRegistryRequest          : uint32_t { bind };
    enum class WlCompositorRequest        : uint32_t { createSurface, createRegion };
    enum class WlSubcompositorRequest     : uint32_t { destroy, getSubsurface };
    enum class WlSubsurfaceRequest        : uint32_t { destroy, setPosition, placeAbove, placeBelow,
                                                       setSync, setDesync };
    enum class WlRegionRequest            : uint32_t { destroy, add, subtract };
    enum class WlSurfaceRequest           : uint32_t { destroy, attach, damage, frame, setOpaqueRegion,
                                                       setInputRegion, commit, setBufferTransform,
                                                       setBufferScale, damageBuffer };
    enum class WlShmRequest               : uint32_t { createPool };
    enum class WlShmPoolRequest           : uint32_t { createBuffer, destroy };
    enum class WlBufferRequest            : uint32_t { destroy };
    enum class WlDataOfferRequest         : uint32_t { accept, receive, destroy, finish, setActions };
    enum class WlDataSourceRequest        : uint32_t { offer, destroy, setActions };
    enum class WlDataDeviceRequest        : uint32_t { startDrag, setSelection, release };
    enum class WlDataDeviceManagerRequest : uint32_t { createDataSource, getDataDevice };
    enum class WlOutputRequest            : uint32_t { release };
    enum class WlSeatRequest              : uint32_t { getPointer, getKeyboard, getTouch, release };
    enum class WlPointerRequest           : uint32_t { setCursor, release };
    enum class WlKeyboardRequest          : uint32_t { release };
    enum class WlTouchRequest             : uint32_t { release };
    enum class XdgWmBaseRequest     : uint32_t { destroy, createPositioner, getXdgSurface, pong };
    enum class XdgSurfaceRequest    : uint32_t { destroy, getToplevel, getPopup, setWindowGeometry, ackConfigure };
    enum class XdgPositionerRequest : uint32_t { destroy, setSize, setAnchorRect, setAnchor, setGravity,
                                                 setConstraintAdjustment, setOffset, setReactive,
                                                 setParentSize, setParentConfigure };
    enum class XdgPopupRequest      : uint32_t { destroy, grab, reposition };
    enum class XdgToplevelRequest   : uint32_t { destroy, setParent, setTitle, setAppId, showWindowMenu,
                                                 move, resize, setMaxSize, setMinSize, setMaximized, unsetMaximized,
                                                 setFullscreen, unsetFullscreen, setMinimized };
    enum class XdgActivationV1Request      : uint32_t { destroy, getActivationToken, activate };
    enum class XdgActivationTokenV1Request : uint32_t { setSerial, setAppId, setSurface, commit, destroy };
    enum class ZxdgDecorationManagerV1Request  : uint32_t { destroy, getToplevelDecoration };
    enum class ZxdgToplevelDecorationV1Request : uint32_t { destroy, setMode, unsetMode };
    enum class ZxdgExporterV2Request : uint32_t { destroy, exportToplevel };
    enum class ZxdgExportedV2Request : uint32_t { destroy };
    enum class ZxdgOutputManagerV1Request : uint32_t { destroy, getXdgOutput };
    enum class ZxdgOutputV1Request        : uint32_t { destroy };
    enum class WpViewporterRequest   : uint32_t { destroy, getViewport };
    enum class WpViewportRequest     : uint32_t { destroy, setSource, setDestination };
    enum class WpFractionalScaleManagerV1Request : uint32_t { destroy, getFractionalScale };
    enum class WpFractionalScaleV1Request        : uint32_t { destroy };
    enum class WpAlphaModifierV1Request          : uint32_t { destroy, getSurface };
    enum class WpAlphaModifierSurfaceV1Request   : uint32_t { destroy, setMultiplier };

    wl_interface xdgPositionerInterface;
    wl_interface xdgPopupInterface;
    wl_interface xdgSurfaceInterface;
    wl_interface xdgToplevelInterface;
    wl_interface xdgWmBaseInterface;
    wl_interface xdgActivationV1Interface;
    wl_interface xdgActivationTokenV1Interface;
    wl_interface zxdgDecorationManagerV1Interface;
    wl_interface zxdgToplevelDecorationV1Interface;
    wl_interface zxdgExporterV2Interface;
    wl_interface zxdgExportedV2Interface;
    wl_interface zxdgOutputManagerV1Interface;
    wl_interface zxdgOutputV1Interface;
    wl_interface wpViewporterInterface;
    wl_interface wpViewportInterface;
    wl_interface wpFractionalScaleManagerV1Interface;
    wl_interface wpFractionalScaleV1Interface;
    wl_interface wpAlphaModifierV1Interface;
    wl_interface wpAlphaModifierSurfaceV1Interface;

    const wl_interface* xdgWmBaseCreatePositionerTypes[] { &xdgPositionerInterface };
    const wl_interface* xdgWmBaseGetXdgSurfaceTypes[]    { &xdgSurfaceInterface, nullptr };

    const wl_message xdgWmBaseRequests[]
    {
        { "destroy", "", nullptr },
        { "create_positioner", "n", xdgWmBaseCreatePositionerTypes },
        { "get_xdg_surface", "no", xdgWmBaseGetXdgSurfaceTypes },
        { "pong", "u", nullptr }
    };

    const wl_message xdgWmBaseEvents[]
    {
        { "ping", "u", nullptr }
    };

    const wl_interface* xdgSurfaceGetToplevelTypes[] { &xdgToplevelInterface };
    const wl_interface* xdgSurfaceGetPopupTypes[]    { &xdgPopupInterface, &xdgSurfaceInterface, &xdgPositionerInterface };

    const wl_message xdgSurfaceRequests[]
    {
        { "destroy", "", nullptr },
        { "get_toplevel", "n", xdgSurfaceGetToplevelTypes },
        { "get_popup", "n?oo", xdgSurfaceGetPopupTypes },
        { "set_window_geometry", "iiii", nullptr },
        { "ack_configure", "u", nullptr }
    };

    const wl_message xdgSurfaceEvents[]
    {
        { "configure", "u", nullptr }
    };

    const wl_interface* xdgToplevelSetParentTypes[]      { &xdgToplevelInterface };
    const wl_interface* xdgToplevelShowWindowMenuTypes[] { nullptr, nullptr, nullptr, nullptr };
    const wl_interface* xdgToplevelMoveTypes[]           { nullptr, nullptr };
    const wl_interface* xdgToplevelResizeTypes[]         { nullptr, nullptr, nullptr };
    const wl_interface* xdgToplevelSetFullscreenTypes[]  { nullptr };

    const wl_message xdgToplevelRequests[]
    {
        { "destroy", "", nullptr },
        { "set_parent", "?o", xdgToplevelSetParentTypes },
        { "set_title", "s", nullptr },
        { "set_app_id", "s", nullptr },
        { "show_window_menu", "ouii", xdgToplevelShowWindowMenuTypes },
        { "move", "ou", xdgToplevelMoveTypes },
        { "resize", "ouu", xdgToplevelResizeTypes },
        { "set_max_size", "ii", nullptr },
        { "set_min_size", "ii", nullptr },
        { "set_maximized", "", nullptr },
        { "unset_maximized", "", nullptr },
        { "set_fullscreen", "?o", xdgToplevelSetFullscreenTypes },
        { "unset_fullscreen", "", nullptr },
        { "set_minimized", "", nullptr }
    };

    const wl_message xdgToplevelEvents[]
    {
        { "configure", "iia", nullptr },
        { "close", "", nullptr },
        { "configure_bounds", "4ii", nullptr },
        { "wm_capabilities", "5a", nullptr }
    };

    const wl_message xdgPositionerRequests[]
    {
        { "destroy", "", nullptr },
        { "set_size", "ii", nullptr },
        { "set_anchor_rect", "iiii", nullptr },
        { "set_anchor", "u", nullptr },
        { "set_gravity", "u", nullptr },
        { "set_constraint_adjustment", "u", nullptr },
        { "set_offset", "ii", nullptr },
        { "set_reactive", "3", nullptr },
        { "set_parent_size", "3ii", nullptr },
        { "set_parent_configure", "3u", nullptr }
    };

    const wl_interface* xdgPopupGrabTypes[]       { nullptr, nullptr };
    const wl_interface* xdgPopupRepositionTypes[] { &xdgPositionerInterface, nullptr };

    const wl_message xdgPopupRequests[]
    {
        { "destroy", "", nullptr },
        { "grab", "ou", xdgPopupGrabTypes },
        { "reposition", "3ou", xdgPopupRepositionTypes }
    };

    const wl_message xdgPopupEvents[]
    {
        { "configure", "iiii", nullptr },
        { "popup_done", "", nullptr },
        { "repositioned", "3u", nullptr }
    };

    const wl_interface* xdgActivationV1GetActivationTokenTypes[] { &xdgActivationTokenV1Interface };
    const wl_interface* xdgActivationV1ActivateTypes[]           { nullptr, nullptr };

    const wl_message xdgActivationV1Requests[]
    {
        { "destroy", "", nullptr },
        { "get_activation_token", "n", xdgActivationV1GetActivationTokenTypes },
        { "activate", "so", xdgActivationV1ActivateTypes }
    };

    const wl_interface* xdgActivationTokenV1SetSerialTypes[]  { nullptr, nullptr };
    const wl_interface* xdgActivationTokenV1SetSurfaceTypes[] { nullptr };

    const wl_message xdgActivationTokenV1Requests[]
    {
        { "set_serial", "uo", xdgActivationTokenV1SetSerialTypes },
        { "set_app_id", "s", nullptr },
        { "set_surface", "o", xdgActivationTokenV1SetSurfaceTypes },
        { "commit", "", nullptr },
        { "destroy", "", nullptr }
    };

    const wl_message xdgActivationTokenV1Events[]
    {
        { "done", "s", nullptr }
    };

    const wl_interface* zxdgDecorationManagerV1GetToplevelDecorationTypes[] { &zxdgToplevelDecorationV1Interface, &xdgToplevelInterface };

    const wl_message zxdgDecorationManagerV1Requests[]
    {
        { "destroy", "", nullptr },
        { "get_toplevel_decoration", "no", zxdgDecorationManagerV1GetToplevelDecorationTypes }
    };

    const wl_message zxdgToplevelDecorationV1Requests[]
    {
        { "destroy", "", nullptr },
        { "set_mode", "u", nullptr },
        { "unset_mode", "", nullptr }
    };

    const wl_message zxdgToplevelDecorationV1Events[]
    {
        { "configure", "u", nullptr }
    };

    const wl_interface* zxdgExporterV2ExportToplevelTypes[] { &zxdgExportedV2Interface, nullptr };

    const wl_message zxdgExporterV2Requests[]
    {
        { "destroy", "", nullptr },
        { "export_toplevel", "no", zxdgExporterV2ExportToplevelTypes }
    };

    const wl_message zxdgExportedV2Requests[] { { "destroy", "", nullptr } };
    const wl_message zxdgExportedV2Events[]   { { "handle", "s", nullptr } };

    const wl_interface* zxdgOutputManagerV1GetXdgOutputTypes[] { &zxdgOutputV1Interface, nullptr };

    const wl_message zxdgOutputManagerV1Requests[]
    {
        { "destroy", "", nullptr },
        { "get_xdg_output", "no", zxdgOutputManagerV1GetXdgOutputTypes }
    };

    const wl_message zxdgOutputV1Requests[] { { "destroy", "", nullptr } };

    const wl_message zxdgOutputV1Events[]
    {
        { "logical_position", "ii", nullptr },
        { "logical_size", "ii", nullptr },
        { "done", "", nullptr },
        { "name", "2s", nullptr },
        { "description", "2s", nullptr }
    };

    const wl_interface* wpViewporterGetViewportTypes[] { &wpViewportInterface, nullptr };

    const wl_message wpViewporterRequests[]
    {
        { "destroy", "", nullptr },
        { "get_viewport", "no", wpViewporterGetViewportTypes }
    };

    const wl_message wpViewportRequests[]
    {
        { "destroy", "", nullptr },
        { "set_source", "ffff", nullptr },
        { "set_destination", "ii", nullptr }
    };

    const wl_interface* wpFractionalScaleManagerV1GetFractionalScaleTypes[] { &wpFractionalScaleV1Interface, nullptr };

    const wl_message wpFractionalScaleManagerV1Requests[]
    {
        { "destroy", "", nullptr },
        { "get_fractional_scale", "no", wpFractionalScaleManagerV1GetFractionalScaleTypes }
    };

    const wl_message wpFractionalScaleV1Requests[] { { "destroy", "", nullptr } };
    const wl_message wpFractionalScaleV1Events[]   { { "preferred_scale", "u", nullptr } };

    const wl_interface* wpAlphaModifierV1GetSurfaceTypes[] { &wpAlphaModifierSurfaceV1Interface, nullptr };

    const wl_message wpAlphaModifierV1Requests[]
    {
        { "destroy", "", nullptr },
        { "get_surface", "no", wpAlphaModifierV1GetSurfaceTypes }
    };

    const wl_message wpAlphaModifierSurfaceV1Requests[]
    {
        { "destroy", "", nullptr },
        { "set_multiplier", "u", nullptr }
    };

    void initialiseInterfaceObjects()
    {
        xdgPositionerInterface = { "xdg_positioner", 6, numElementsInArray (xdgPositionerRequests), xdgPositionerRequests, 0, nullptr };
        xdgPopupInterface      = { "xdg_popup",      6, numElementsInArray (xdgPopupRequests),      xdgPopupRequests,      numElementsInArray (xdgPopupEvents),      xdgPopupEvents };
        xdgSurfaceInterface    = { "xdg_surface",    6, numElementsInArray (xdgSurfaceRequests),    xdgSurfaceRequests,    numElementsInArray (xdgSurfaceEvents),    xdgSurfaceEvents };
        xdgToplevelInterface   = { "xdg_toplevel",   6, numElementsInArray (xdgToplevelRequests),   xdgToplevelRequests,   numElementsInArray (xdgToplevelEvents),   xdgToplevelEvents };
        xdgWmBaseInterface     = { "xdg_wm_base",    6, numElementsInArray (xdgWmBaseRequests),     xdgWmBaseRequests,     numElementsInArray (xdgWmBaseEvents),     xdgWmBaseEvents };
        xdgActivationV1Interface      = { "xdg_activation_v1", 1, numElementsInArray (xdgActivationV1Requests), xdgActivationV1Requests, 0, nullptr };
        xdgActivationTokenV1Interface = { "xdg_activation_token_v1", 1, numElementsInArray (xdgActivationTokenV1Requests), xdgActivationTokenV1Requests, numElementsInArray (xdgActivationTokenV1Events), xdgActivationTokenV1Events };
        zxdgDecorationManagerV1Interface  = { "zxdg_decoration_manager_v1", 1, numElementsInArray (zxdgDecorationManagerV1Requests), zxdgDecorationManagerV1Requests, 0, nullptr };
        zxdgToplevelDecorationV1Interface = { "zxdg_toplevel_decoration_v1", 1, numElementsInArray (zxdgToplevelDecorationV1Requests), zxdgToplevelDecorationV1Requests, numElementsInArray (zxdgToplevelDecorationV1Events), zxdgToplevelDecorationV1Events };
        zxdgExporterV2Interface = { "zxdg_exporter_v2", 1, numElementsInArray (zxdgExporterV2Requests), zxdgExporterV2Requests, 0, nullptr };
        zxdgExportedV2Interface = { "zxdg_exported_v2", 1, numElementsInArray (zxdgExportedV2Requests), zxdgExportedV2Requests, numElementsInArray (zxdgExportedV2Events), zxdgExportedV2Events };
        zxdgOutputManagerV1Interface = { "zxdg_output_manager_v1", 3, numElementsInArray (zxdgOutputManagerV1Requests), zxdgOutputManagerV1Requests, 0, nullptr };
        zxdgOutputV1Interface = { "zxdg_output_v1", 3, numElementsInArray (zxdgOutputV1Requests), zxdgOutputV1Requests, numElementsInArray (zxdgOutputV1Events), zxdgOutputV1Events };
        wpViewporterInterface   = { "wp_viewporter", 1, numElementsInArray (wpViewporterRequests), wpViewporterRequests, 0, nullptr };
        wpViewportInterface     = { "wp_viewport", 1, numElementsInArray (wpViewportRequests), wpViewportRequests, 0, nullptr };
        wpFractionalScaleManagerV1Interface = { "wp_fractional_scale_manager_v1", 1, numElementsInArray (wpFractionalScaleManagerV1Requests), wpFractionalScaleManagerV1Requests, 0, nullptr };
        wpFractionalScaleV1Interface        = { "wp_fractional_scale_v1", 1, numElementsInArray (wpFractionalScaleV1Requests), wpFractionalScaleV1Requests, numElementsInArray (wpFractionalScaleV1Events), wpFractionalScaleV1Events };
        wpAlphaModifierV1Interface        = { "wp_alpha_modifier_v1", 1, numElementsInArray (wpAlphaModifierV1Requests), wpAlphaModifierV1Requests, 0, nullptr };
        wpAlphaModifierSurfaceV1Interface = { "wp_alpha_modifier_surface_v1", 1, numElementsInArray (wpAlphaModifierSurfaceV1Requests), wpAlphaModifierSurfaceV1Requests, 0, nullptr };
    }

    uint32_t getVersion (wl_proxy* proxy)
    {
        return WaylandSymbols::getInstance()->wlProxyGetVersion (proxy);
    }

    template <typename Object>
    wl_proxy* proxy (Object* object)
    {
        return reinterpret_cast<wl_proxy*> (object);
    }

    template <typename Object>
    Object* object (wl_proxy* proxyIn)
    {
        return reinterpret_cast<Object*> (proxyIn);
    }

    void* registryBind (wl_registry* registry, uint32_t name, const wl_interface* interface, uint32_t version)
    {
        return WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (registry),
                                                                  toUnderlyingType (WlRegistryRequest::bind),
                                                                  interface,
                                                                  version,
                                                                  0,
                                                                  name,
                                                                  interface->name,
                                                                  version,
                                                                  nullptr);
    }
}

//==============================================================================
Point<float> fixedToPoint (int32_t x, int32_t y)
{
    return { (float) (x / 256.0), (float) (y / 256.0) };
}

void initialiseInterfaces (const WaylandSymbols& symbols)
{
    initialiseInterfaceObjects();
    xdgWmBaseGetXdgSurfaceTypes[1]    = symbols.wlSurfaceInterface;
    xdgPopupGrabTypes[0]              = symbols.wlSeatInterface;
    xdgToplevelShowWindowMenuTypes[0] = symbols.wlSeatInterface;
    xdgToplevelMoveTypes[0]           = symbols.wlSeatInterface;
    xdgToplevelResizeTypes[0]         = symbols.wlSeatInterface;
    xdgToplevelSetFullscreenTypes[0]  = symbols.wlOutputInterface;
    xdgActivationV1ActivateTypes[1]        = symbols.wlSurfaceInterface;
    xdgActivationTokenV1SetSerialTypes[1]  = symbols.wlSeatInterface;
    xdgActivationTokenV1SetSurfaceTypes[0] = symbols.wlSurfaceInterface;
    zxdgExporterV2ExportToplevelTypes[1] = symbols.wlSurfaceInterface;
    zxdgOutputManagerV1GetXdgOutputTypes[1] = symbols.wlOutputInterface;
    wpViewporterGetViewportTypes[1]   = symbols.wlSurfaceInterface;
    wpFractionalScaleManagerV1GetFractionalScaleTypes[1] = symbols.wlSurfaceInterface;
    wpAlphaModifierV1GetSurfaceTypes[1] = symbols.wlSurfaceInterface;
}

wl_registry* wlDisplayGetRegistry (wl_display* display)
{
    return object<wl_registry> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (display),
                                                                                   toUnderlyingType (WlDisplayRequest::getRegistry),
                                                                                   WaylandSymbols::getInstance()->wlRegistryInterface,
                                                                                   1,
                                                                                   0,
                                                                                   nullptr));
}

wl_callback* wlDisplaySync (wl_display* display)
{
    return object<wl_callback> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (display),
                                                                                   toUnderlyingType (WlDisplayRequest::sync),
                                                                                   WaylandSymbols::getInstance()->wlCallbackInterface,
                                                                                   1,
                                                                                   0,
                                                                                   nullptr));
}

int wlRegistryAddListener (wl_registry* registry, const wl_registry_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (registry),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_registry_listener*> (listener)),
                                                             data);
}

void wlRegistryDestroy (wl_registry* registry)
{
    WaylandSymbols::getInstance()->wlProxyDestroy (proxy (registry));
}

wl_compositor* bindCompositor (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto* compositorInterface = WaylandSymbols::getInstance()->wlCompositorInterface;
    // Version 6 is the current end of wl_compositor. Use the newest version shared by JUCE,
    // the compositor, and the Wayland client library loaded at runtime.
    constexpr uint32_t highestSupportedVersion = 6;
    const auto bindVersion = jmin (version, highestSupportedVersion, (uint32_t) compositorInterface->version);
    return static_cast<wl_compositor*> (registryBind (registry, name,
                                                      compositorInterface,
                                                      bindVersion));
}

wl_surface* wlCompositorCreateSurface (wl_compositor* compositor)
{
    return object<wl_surface> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (compositor),
                                                                                  toUnderlyingType (WlCompositorRequest::createSurface),
                                                                                  WaylandSymbols::getInstance()->wlSurfaceInterface,
                                                                                  getVersion (proxy (compositor)),
                                                                                  0,
                                                                                  nullptr));
}

wl_region* wlCompositorCreateRegion (wl_compositor* compositor)
{
    return object<wl_region> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (compositor),
                                                                                 toUnderlyingType (WlCompositorRequest::createRegion),
                                                                                 WaylandSymbols::getInstance()->wlRegionInterface,
                                                                                 getVersion (proxy (compositor)),
                                                                                 0,
                                                                                 nullptr));
}

void wlCompositorDestroy (wl_compositor* compositor)
{
    WaylandSymbols::getInstance()->wlProxyDestroy (proxy (compositor));
}

wl_subcompositor* bindSubcompositor (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto* subcompositorInterface = WaylandSymbols::getInstance()->wlSubcompositorInterface;
    const auto bindVersion = jmin (version, (uint32_t) 1, (uint32_t) subcompositorInterface->version);
    return static_cast<wl_subcompositor*> (registryBind (registry, name,
                                                         subcompositorInterface,
                                                         bindVersion));
}

wl_subsurface* wlSubcompositorGetSubsurface (wl_subcompositor* subcompositor,
                                             wl_surface* surface,
                                             wl_surface* parent)
{
    return object<wl_subsurface> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (subcompositor),
                                                                                     toUnderlyingType (WlSubcompositorRequest::getSubsurface),
                                                                                     WaylandSymbols::getInstance()->wlSubsurfaceInterface,
                                                                                     getVersion (proxy (subcompositor)),
                                                                                     0,
                                                                                     nullptr,
                                                                                     surface,
                                                                                     parent));
}

void wlSubcompositorDestroy (wl_subcompositor* subcompositor)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (subcompositor),
                                                        toUnderlyingType (WlSubcompositorRequest::destroy),
                                                        nullptr,
                                                        getVersion (proxy (subcompositor)),
                                                        destroyFlag);
}

void wlSubsurfaceDestroy (wl_subsurface* subsurface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (subsurface),
                                                        toUnderlyingType (WlSubsurfaceRequest::destroy),
                                                        nullptr,
                                                        getVersion (proxy (subsurface)),
                                                        destroyFlag);
}

void wlSubsurfaceSetPosition (wl_subsurface* subsurface, int32_t x, int32_t y)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (subsurface),
                                                        toUnderlyingType (WlSubsurfaceRequest::setPosition),
                                                        nullptr,
                                                        getVersion (proxy (subsurface)),
                                                        0,
                                                        x,
                                                        y);
}

void wlSubsurfaceSetDesync (wl_subsurface* subsurface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (subsurface),
                                                        toUnderlyingType (WlSubsurfaceRequest::setDesync),
                                                        nullptr,
                                                        getVersion (proxy (subsurface)),
                                                        0);
}

int wlSurfaceAddListener (wl_surface* surface, const wl_surface_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (surface),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_surface_listener*> (listener)),
                                                             data);
}

void wlSurfaceDestroy (wl_surface* surface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::destroy), nullptr, getVersion (proxy (surface)), destroyFlag);
}

void wlSurfaceAttach (wl_surface* surface, wl_buffer* buffer, int32_t x, int32_t y)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::attach), nullptr, getVersion (proxy (surface)), 0, buffer, x, y);
}

void wlSurfaceDamageBuffer (wl_surface* surface, int32_t x, int32_t y, int32_t width, int32_t height)
{
    const auto version = getVersion (proxy (surface));

    if (version >= 4)
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::damageBuffer), nullptr, version, 0, x, y, width, height);
    else
        // wl_surface.damage measures in surface coordinates rather than buffer pixels, so damaging everything is the only correct translation.
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::damage), nullptr, version, 0, 0, 0,
                                                           std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
}

wl_callback* wlSurfaceFrame (wl_surface* surface)
{
    return object<wl_callback> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface),
                                                                                   toUnderlyingType (WlSurfaceRequest::frame),
                                                                                   WaylandSymbols::getInstance()->wlCallbackInterface,
                                                                                   getVersion (proxy (surface)),
                                                                                   0,
                                                                                   nullptr));
}

void wlSurfaceSetOpaqueRegion (wl_surface* surface, wl_region* region)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::setOpaqueRegion), nullptr, getVersion (proxy (surface)), 0, region);
}

void wlSurfaceSetInputRegion (wl_surface* surface, wl_region* region)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::setInputRegion), nullptr, getVersion (proxy (surface)), 0, region);
}

void wlSurfaceSetBufferScale (wl_surface* surface, int32_t scale)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::setBufferScale), nullptr, getVersion (proxy (surface)), 0, scale);
}

void wlSurfaceCommit (wl_surface* surface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (WlSurfaceRequest::commit), nullptr, getVersion (proxy (surface)), 0);
}

void wlRegionAdd (wl_region* region, int32_t x, int32_t y, int32_t width, int32_t height)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (region), toUnderlyingType (WlRegionRequest::add), nullptr, getVersion (proxy (region)), 0, x, y, width, height);
}

void wlRegionDestroy (wl_region* region)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (region), toUnderlyingType (WlRegionRequest::destroy), nullptr, getVersion (proxy (region)), destroyFlag);
}

int wlCallbackAddListener (wl_callback* callback, const wl_callback_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (callback),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_callback_listener*> (listener)),
                                                             data);
}

void wlCallbackDestroy (wl_callback* callback)
{
    WaylandSymbols::getInstance()->wlProxyDestroy (proxy (callback));
}

wl_shm* bindShm (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto* shmInterface = WaylandSymbols::getInstance()->wlShmInterface;
    const auto bindVersion = jmin (version, (uint32_t) 1, (uint32_t) shmInterface->version);
    return static_cast<wl_shm*> (registryBind (registry, name,
                                               shmInterface,
                                               bindVersion));
}

wl_shm_pool* wlShmCreatePool (wl_shm* shm, int fd, int32_t size)
{
    return object<wl_shm_pool> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (shm),
                                                                                   toUnderlyingType (WlShmRequest::createPool),
                                                                                   WaylandSymbols::getInstance()->wlShmPoolInterface,
                                                                                   getVersion (proxy (shm)),
                                                                                   0,
                                                                                   nullptr,
                                                                                   fd,
                                                                                   size));
}

wl_buffer* wlShmPoolCreateBuffer (wl_shm_pool* pool, int32_t offset, int32_t width, int32_t height, int32_t stride, uint32_t format)
{
    return object<wl_buffer> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (pool),
                                                                                 toUnderlyingType (WlShmPoolRequest::createBuffer),
                                                                                 WaylandSymbols::getInstance()->wlBufferInterface,
                                                                                 getVersion (proxy (pool)),
                                                                                 0,
                                                                                 nullptr,
                                                                                 offset,
                                                                                 width,
                                                                                 height,
                                                                                 stride,
                                                                                 format));
}

void wlShmDestroy (wl_shm* shm)
{
    WaylandSymbols::getInstance()->wlProxyDestroy (proxy (shm));
}

void wlShmPoolDestroy (wl_shm_pool* pool)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (pool), toUnderlyingType (WlShmPoolRequest::destroy), nullptr, getVersion (proxy (pool)), destroyFlag);
}

int wlBufferAddListener (wl_buffer* buffer, const wl_buffer_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (buffer),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_buffer_listener*> (listener)),
                                                             data);
}

void wlBufferDestroy (wl_buffer* buffer)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (buffer), toUnderlyingType (WlBufferRequest::destroy), nullptr, getVersion (proxy (buffer)), destroyFlag);
}

wl_data_device_manager* bindDataDeviceManager (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto* interface = WaylandSymbols::getInstance()->wlDataDeviceManagerInterface;
    const auto bindVersion = jmin (version, (uint32_t) interface->version);
    return static_cast<wl_data_device_manager*> (registryBind (registry, name, interface, bindVersion));
}

wl_data_source* wlDataDeviceManagerCreateDataSource (wl_data_device_manager* manager)
{
    return object<wl_data_source> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager),
                                                                                      toUnderlyingType (WlDataDeviceManagerRequest::createDataSource),
                                                                                      WaylandSymbols::getInstance()->wlDataSourceInterface,
                                                                                      getVersion (proxy (manager)),
                                                                                      0,
                                                                                      nullptr));
}

wl_data_device* wlDataDeviceManagerGetDataDevice (wl_data_device_manager* manager, wl_seat* seat)
{
    return object<wl_data_device> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager),
                                                                                      toUnderlyingType (WlDataDeviceManagerRequest::getDataDevice),
                                                                                      WaylandSymbols::getInstance()->wlDataDeviceInterface,
                                                                                      getVersion (proxy (manager)),
                                                                                      0,
                                                                                      nullptr,
                                                                                      seat));
}

void destroyDataDeviceManager (wl_data_device_manager* manager)
{
    WaylandSymbols::getInstance()->wlProxyDestroy (proxy (manager));
}

int wlDataSourceAddListener (wl_data_source* source, const wl_data_source_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (source),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_data_source_listener*> (listener)),
                                                             data);
}

uint32_t getDataSourceVersion (wl_data_source* source)
{
    return getVersion (proxy (source));
}

void wlDataSourceOffer (wl_data_source* source, const char* mimeType)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (source), toUnderlyingType (WlDataSourceRequest::offer), nullptr, getVersion (proxy (source)), 0, mimeType);
}

void wlDataSourceSetActions (wl_data_source* source, uint32_t actions)
{
    const auto version = getVersion (proxy (source));

    if (version < dataDeviceActionNegotiationVersion)
        return;

    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (source), toUnderlyingType (WlDataSourceRequest::setActions), nullptr, version, 0, actions);
}

void wlDataSourceDestroy (wl_data_source* source)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (source), toUnderlyingType (WlDataSourceRequest::destroy), nullptr, getVersion (proxy (source)), destroyFlag);
}

int wlDataOfferAddListener (wl_data_offer* offer, const wl_data_offer_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (offer),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_data_offer_listener*> (listener)),
                                                             data);
}

uint32_t getDataOfferVersion (wl_data_offer* offer)
{
    return getVersion (proxy (offer));
}

void wlDataOfferAccept (wl_data_offer* offer, uint32_t serial, const char* mimeType)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (offer), toUnderlyingType (WlDataOfferRequest::accept), nullptr, getVersion (proxy (offer)), 0, serial, mimeType);
}

void wlDataOfferReceive (wl_data_offer* offer, const char* mimeType, int fd)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (offer), toUnderlyingType (WlDataOfferRequest::receive), nullptr, getVersion (proxy (offer)), 0, mimeType, fd);
}

bool wlDataOfferFinish (wl_data_offer* offer)
{
    const auto version = getVersion (proxy (offer));

    if (version < dataDeviceActionNegotiationVersion)
        return false;

    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (offer), toUnderlyingType (WlDataOfferRequest::finish), nullptr, version, 0);
    return true;
}

bool wlDataOfferSetActions (wl_data_offer* offer, uint32_t actions, uint32_t preferredAction)
{
    const auto version = getVersion (proxy (offer));

    if (version < dataDeviceActionNegotiationVersion)
        return false;

    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (offer), toUnderlyingType (WlDataOfferRequest::setActions), nullptr, version, 0, actions, preferredAction);
    return true;
}

void wlDataOfferDestroy (wl_data_offer* offer)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (offer), toUnderlyingType (WlDataOfferRequest::destroy), nullptr, getVersion (proxy (offer)), destroyFlag);
}

int wlDataDeviceAddListener (wl_data_device* device, const wl_data_device_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (device),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_data_device_listener*> (listener)),
                                                             data);
}

void wlDataDeviceStartDrag (wl_data_device* device, wl_data_source* source, wl_surface* origin,
                            wl_surface* icon, uint32_t serial)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (device), toUnderlyingType (WlDataDeviceRequest::startDrag), nullptr,
                                                        getVersion (proxy (device)), 0, source, origin, icon, serial);
}

void wlDataDeviceSetSelection (wl_data_device* device, wl_data_source* source, uint32_t serial)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (device), toUnderlyingType (WlDataDeviceRequest::setSelection), nullptr,
                                                        getVersion (proxy (device)), 0, source, serial);
}

void destroyDataDevice (wl_data_device* device)
{
    const auto version = getVersion (proxy (device));

    // wl_data_device.release was added in version 2. Older proxies must be destroyed locally.
    if (version < 2)
    {
        WaylandSymbols::getInstance()->wlProxyDestroy (proxy (device));
        return;
    }

    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (device), toUnderlyingType (WlDataDeviceRequest::release), nullptr, version, destroyFlag);
}

wl_output* bindOutput (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto* outputInterface = WaylandSymbols::getInstance()->wlOutputInterface;
    // wl_output currently supports versions up to 4.
    constexpr uint32_t highestSupportedVersion = 4;
    const auto bindVersion = jmin (version, highestSupportedVersion, (uint32_t) outputInterface->version);
    return static_cast<wl_output*> (registryBind (registry, name,
                                                  outputInterface,
                                                  bindVersion));
}

uint32_t getOutputVersion (wl_output* output)
{
    return getVersion (proxy (output));
}

int wlOutputAddListener (wl_output* output, const wl_output_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (output),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_output_listener*> (listener)),
                                                             data);
}

void destroyOutput (wl_output* output)
{
    const auto version = getVersion (proxy (output));

    // wl_output.release exists only from version 3.
    if (version >= 3)
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (output), toUnderlyingType (WlOutputRequest::release), nullptr, version, destroyFlag);
    else
        WaylandSymbols::getInstance()->wlProxyDestroy (proxy (output));
}

zxdg_output_manager_v1* bindZxdgOutputManagerV1 (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) 3);
    return static_cast<zxdg_output_manager_v1*> (registryBind (registry, name, &zxdgOutputManagerV1Interface, bindVersion));
}

void zxdgOutputManagerV1Destroy (zxdg_output_manager_v1* manager)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager), toUnderlyingType (ZxdgOutputManagerV1Request::destroy), nullptr, getVersion (proxy (manager)), destroyFlag);
}

zxdg_output_v1* zxdgOutputManagerV1GetXdgOutput (zxdg_output_manager_v1* manager, wl_output* output)
{
    return object<zxdg_output_v1> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager),
                                                                                      toUnderlyingType (ZxdgOutputManagerV1Request::getXdgOutput),
                                                                                      &zxdgOutputV1Interface,
                                                                                      getVersion (proxy (manager)),
                                                                                      0,
                                                                                      nullptr,
                                                                                      output));
}

uint32_t getXdgOutputVersion (zxdg_output_v1* output)
{
    return getVersion (proxy (output));
}

int zxdgOutputV1AddListener (zxdg_output_v1* output, const zxdg_output_v1_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (output),
                                                             reinterpret_cast<void (**) (void)> (const_cast<zxdg_output_v1_listener*> (listener)),
                                                             data);
}

void zxdgOutputV1Destroy (zxdg_output_v1* output)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (output), toUnderlyingType (ZxdgOutputV1Request::destroy), nullptr, getVersion (proxy (output)), destroyFlag);
}

wl_seat* bindSeat (wl_registry* registry, uint32_t name, uint32_t version)
{
    // The device proxies inherit the seat version, so raise this only together with the listener structs.
    // Clamping to seatInterface version keeps an older libwayland from receiving events it cannot dispatch.
    //
    //   v1          capabilities, get_pointer/keyboard/touch, core pointer/keyboard/touch events
    //   v2          wl_seat.name
    //   v3          release destructors on wl_pointer/wl_keyboard/wl_touch
    //   v4          wl_keyboard.repeat_info (rate and delay, rate 0 means no repeat)
    //   v5          wl_seat.release, pointer frame, axis_source, axis_stop, axis_discrete
    //   v6          touch shape and orientation, axis_source gains wheel_tilt
    //   v7          no event changes (keymap fd must be mapped MAP_PRIVATE)
    //   v8   1.21   pointer axis_value120 (axis_discrete no longer sent)
    //   v9   1.22   pointer axis_relative_direction
    //   v10  1.24   key state gains repeated (compositor-driven repeat when repeat_info rate 0)
    //   v11  1.26   pointer warp event
    const auto* seatInterface = WaylandSymbols::getInstance()->wlSeatInterface;
    const auto bindVersion = jmin (version, (uint32_t) 10, (uint32_t) seatInterface->version);
    return static_cast<wl_seat*> (registryBind (registry, name, seatInterface, bindVersion));
}

int wlSeatAddListener (wl_seat* seat, const wl_seat_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (seat),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_seat_listener*> (listener)),
                                                             data);
}

wl_pointer* wlSeatGetPointer (wl_seat* seat)
{
    return object<wl_pointer> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (seat),
                                                                                  toUnderlyingType (WlSeatRequest::getPointer),
                                                                                  WaylandSymbols::getInstance()->wlPointerInterface,
                                                                                  getVersion (proxy (seat)),
                                                                                  0,
                                                                                  nullptr));
}

wl_keyboard* wlSeatGetKeyboard (wl_seat* seat)
{
    return object<wl_keyboard> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (seat),
                                                                                   toUnderlyingType (WlSeatRequest::getKeyboard),
                                                                                   WaylandSymbols::getInstance()->wlKeyboardInterface,
                                                                                   getVersion (proxy (seat)),
                                                                                   0,
                                                                                   nullptr));
}

wl_touch* wlSeatGetTouch (wl_seat* seat)
{
    return object<wl_touch> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (seat),
                                                                                toUnderlyingType (WlSeatRequest::getTouch),
                                                                                WaylandSymbols::getInstance()->wlTouchInterface,
                                                                                getVersion (proxy (seat)),
                                                                                0,
                                                                                nullptr));
}

void destroySeat (wl_seat* seat)
{
    const auto version = getVersion (proxy (seat));

    // wl_seat.release exists only from version 5.
    if (version >= 5)
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (seat), toUnderlyingType (WlSeatRequest::release), nullptr, version, destroyFlag);
    else
        WaylandSymbols::getInstance()->wlProxyDestroy (proxy (seat));
}

void wlPointerSetCursor (wl_pointer* pointer, uint32_t serial, wl_surface* surface, int32_t hotspotX, int32_t hotspotY)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (pointer), toUnderlyingType (WlPointerRequest::setCursor), nullptr, getVersion (proxy (pointer)), 0, serial, surface, hotspotX, hotspotY);
}

int wlPointerAddListener (wl_pointer* pointer, const wl_pointer_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (pointer),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_pointer_listener*> (listener)),
                                                             data);
}

void destroyPointer (wl_pointer* pointer)
{
    const auto version = getVersion (proxy (pointer));

    // wl_pointer.release exists only from version 3.
    if (version >= 3)
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (pointer), toUnderlyingType (WlPointerRequest::release), nullptr, version, destroyFlag);
    else
        WaylandSymbols::getInstance()->wlProxyDestroy (proxy (pointer));
}

int wlKeyboardAddListener (wl_keyboard* keyboard, const wl_keyboard_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (keyboard),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_keyboard_listener*> (listener)),
                                                             data);
}

void destroyKeyboard (wl_keyboard* keyboard)
{
    const auto version = getVersion (proxy (keyboard));

    // wl_keyboard.release exists only from version 3.
    if (version >= 3)
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (keyboard), toUnderlyingType (WlKeyboardRequest::release), nullptr, version, destroyFlag);
    else
        WaylandSymbols::getInstance()->wlProxyDestroy (proxy (keyboard));
}

int wlTouchAddListener (wl_touch* touch, const wl_touch_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (touch),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wl_touch_listener*> (listener)),
                                                             data);
}

void destroyTouch (wl_touch* touch)
{
    const auto version = getVersion (proxy (touch));

    // wl_touch.release exists only from version 3.
    if (version >= 3)
        WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (touch), toUnderlyingType (WlTouchRequest::release), nullptr, version, destroyFlag);
    else
        WaylandSymbols::getInstance()->wlProxyDestroy (proxy (touch));
}

xdg_wm_base* bindXdgWmBase (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) xdgWmBaseInterface.version);
    return static_cast<xdg_wm_base*> (registryBind (registry, name, &xdgWmBaseInterface, bindVersion));
}

int xdgWmBaseAddListener (xdg_wm_base* wmBase, const xdg_wm_base_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (wmBase),
                                                             reinterpret_cast<void (**) (void)> (const_cast<xdg_wm_base_listener*> (listener)),
                                                             data);
}

void xdgWmBasePong (xdg_wm_base* wmBase, uint32_t serial)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (wmBase), toUnderlyingType (XdgWmBaseRequest::pong), nullptr, getVersion (proxy (wmBase)), 0, serial);
}

xdg_positioner* xdgWmBaseCreatePositioner (xdg_wm_base* wmBase)
{
    return object<xdg_positioner> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (wmBase),
                                                                                       toUnderlyingType (XdgWmBaseRequest::createPositioner),
                                                                                       &xdgPositionerInterface,
                                                                                       getVersion (proxy (wmBase)),
                                                                                       0,
                                                                                       nullptr));
}

void xdgWmBaseDestroy (xdg_wm_base* wmBase)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (wmBase), toUnderlyingType (XdgWmBaseRequest::destroy), nullptr, getVersion (proxy (wmBase)), destroyFlag);
}

xdg_surface* xdgWmBaseGetXdgSurface (xdg_wm_base* wmBase, wl_surface* surface)
{
    return object<xdg_surface> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (wmBase),
                                                                                   toUnderlyingType (XdgWmBaseRequest::getXdgSurface),
                                                                                   &xdgSurfaceInterface,
                                                                                   getVersion (proxy (wmBase)),
                                                                                   0,
                                                                                   nullptr,
                                                                                   surface));
}

int xdgSurfaceAddListener (xdg_surface* surface, const xdg_surface_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (surface),
                                                             reinterpret_cast<void (**) (void)> (const_cast<xdg_surface_listener*> (listener)),
                                                             data);
}

xdg_toplevel* xdgSurfaceGetToplevel (xdg_surface* surface)
{
    return object<xdg_toplevel> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface),
                                                                                    toUnderlyingType (XdgSurfaceRequest::getToplevel),
                                                                                    &xdgToplevelInterface,
                                                                                    getVersion (proxy (surface)),
                                                                                    0,
                                                                                    nullptr));
}

xdg_popup* xdgSurfaceGetPopup (xdg_surface* surface, xdg_surface* parent, xdg_positioner* positioner)
{
    return object<xdg_popup> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface),
                                                                                 toUnderlyingType (XdgSurfaceRequest::getPopup),
                                                                                 &xdgPopupInterface,
                                                                                 getVersion (proxy (surface)),
                                                                                 0,
                                                                                 nullptr,
                                                                                 parent,
                                                                                 positioner));
}

void xdgSurfaceAckConfigure (xdg_surface* surface, uint32_t serial)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (XdgSurfaceRequest::ackConfigure), nullptr, getVersion (proxy (surface)), 0, serial);
}

void xdgSurfaceDestroy (xdg_surface* surface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (surface), toUnderlyingType (XdgSurfaceRequest::destroy), nullptr, getVersion (proxy (surface)), destroyFlag);
}

void xdgPositionerSetSize (xdg_positioner* positioner, int32_t width, int32_t height)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::setSize), nullptr, getVersion (proxy (positioner)), 0, width, height);
}

void xdgPositionerSetAnchorRect (xdg_positioner* positioner, int32_t x, int32_t y, int32_t width, int32_t height)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::setAnchorRect), nullptr, getVersion (proxy (positioner)), 0, x, y, width, height);
}

void xdgPositionerSetAnchor (xdg_positioner* positioner, uint32_t anchor)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::setAnchor), nullptr, getVersion (proxy (positioner)), 0, anchor);
}

void xdgPositionerSetGravity (xdg_positioner* positioner, uint32_t gravity)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::setGravity), nullptr, getVersion (proxy (positioner)), 0, gravity);
}

void xdgPositionerSetConstraintAdjustment (xdg_positioner* positioner, uint32_t adjustment)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::setConstraintAdjustment), nullptr, getVersion (proxy (positioner)), 0, adjustment);
}

void xdgPositionerSetOffset (xdg_positioner* positioner, int32_t x, int32_t y)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::setOffset), nullptr, getVersion (proxy (positioner)), 0, x, y);
}

void xdgPositionerDestroy (xdg_positioner* positioner)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (positioner), toUnderlyingType (XdgPositionerRequest::destroy), nullptr, getVersion (proxy (positioner)), destroyFlag);
}

int xdgPopupAddListener (xdg_popup* popup, const xdg_popup_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (popup),
                                                             reinterpret_cast<void (**) (void)> (const_cast<xdg_popup_listener*> (listener)),
                                                             data);
}

void xdgPopupGrab (xdg_popup* popup, wl_seat* seat, uint32_t serial)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (popup), toUnderlyingType (XdgPopupRequest::grab), nullptr, getVersion (proxy (popup)), 0, seat, serial);
}

bool xdgPopupReposition (xdg_popup* popup, xdg_positioner* positioner, uint32_t token)
{
    const auto version = getVersion (proxy (popup));

    if (version < 3)
        return false;

    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (popup), toUnderlyingType (XdgPopupRequest::reposition), nullptr, version, 0, positioner, token);
    return true;
}

void xdgPopupDestroy (xdg_popup* popup)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (popup), toUnderlyingType (XdgPopupRequest::destroy), nullptr, getVersion (proxy (popup)), destroyFlag);
}

int xdgToplevelAddListener (xdg_toplevel* toplevel, const xdg_toplevel_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (toplevel),
                                                             reinterpret_cast<void (**) (void)> (const_cast<xdg_toplevel_listener*> (listener)),
                                                             data);
}

void xdgToplevelSetTitle (xdg_toplevel* toplevel, const char* title)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::setTitle), nullptr, getVersion (proxy (toplevel)), 0, title);
}

void xdgToplevelSetAppId (xdg_toplevel* toplevel, const char* appId)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::setAppId), nullptr, getVersion (proxy (toplevel)), 0, appId);
}

void xdgToplevelMove (xdg_toplevel* toplevel, wl_seat* seat, uint32_t serial)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::move), nullptr, getVersion (proxy (toplevel)), 0, seat, serial);
}

void xdgToplevelResize (xdg_toplevel* toplevel, wl_seat* seat, uint32_t serial, uint32_t resizeEdge)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::resize), nullptr, getVersion (proxy (toplevel)), 0, seat, serial, resizeEdge);
}

void xdgToplevelSetMaxSize (xdg_toplevel* toplevel, int32_t width, int32_t height)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::setMaxSize), nullptr, getVersion (proxy (toplevel)), 0, width, height);
}

void xdgToplevelSetMinSize (xdg_toplevel* toplevel, int32_t width, int32_t height)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::setMinSize), nullptr, getVersion (proxy (toplevel)), 0, width, height);
}

void xdgToplevelDestroy (xdg_toplevel* toplevel)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::destroy), nullptr, getVersion (proxy (toplevel)), destroyFlag);
}

void xdgToplevelSetMinimized (xdg_toplevel* toplevel)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::setMinimized), nullptr, getVersion (proxy (toplevel)), 0);
}

void xdgToplevelSetFullscreen (xdg_toplevel* toplevel)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::setFullscreen), nullptr, getVersion (proxy (toplevel)), 0, (wl_output*) nullptr);
}

void xdgToplevelUnsetFullscreen (xdg_toplevel* toplevel)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (toplevel), toUnderlyingType (XdgToplevelRequest::unsetFullscreen), nullptr, getVersion (proxy (toplevel)), 0);
}

xdg_activation_v1* bindXdgActivationV1 (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) xdgActivationV1Interface.version);
    return static_cast<xdg_activation_v1*> (registryBind (registry, name, &xdgActivationV1Interface, bindVersion));
}

void xdgActivationV1Destroy (xdg_activation_v1* activation)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (activation), toUnderlyingType (XdgActivationV1Request::destroy), nullptr, getVersion (proxy (activation)), destroyFlag);
}

xdg_activation_token_v1* xdgActivationV1GetActivationToken (xdg_activation_v1* activation)
{
    return object<xdg_activation_token_v1> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (activation),
                                                                                                toUnderlyingType (XdgActivationV1Request::getActivationToken),
                                                                                                &xdgActivationTokenV1Interface,
                                                                                                getVersion (proxy (activation)),
                                                                                                0,
                                                                                                nullptr));
}

void xdgActivationV1Activate (xdg_activation_v1* activation, const char* token, wl_surface* surface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (activation), toUnderlyingType (XdgActivationV1Request::activate), nullptr, getVersion (proxy (activation)), 0, token, surface);
}

int xdgActivationTokenV1AddListener (xdg_activation_token_v1* token, const xdg_activation_token_v1_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (token),
                                                             reinterpret_cast<void (**) (void)> (const_cast<xdg_activation_token_v1_listener*> (listener)),
                                                             data);
}

void xdgActivationTokenV1SetSerial (xdg_activation_token_v1* token, uint32_t serial, wl_seat* seat)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (token), toUnderlyingType (XdgActivationTokenV1Request::setSerial), nullptr, getVersion (proxy (token)), 0, serial, seat);
}

void xdgActivationTokenV1SetSurface (xdg_activation_token_v1* token, wl_surface* surface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (token), toUnderlyingType (XdgActivationTokenV1Request::setSurface), nullptr, getVersion (proxy (token)), 0, surface);
}

void xdgActivationTokenV1Commit (xdg_activation_token_v1* token)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (token), toUnderlyingType (XdgActivationTokenV1Request::commit), nullptr, getVersion (proxy (token)), 0);
}

void xdgActivationTokenV1Destroy (xdg_activation_token_v1* token)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (token), toUnderlyingType (XdgActivationTokenV1Request::destroy), nullptr, getVersion (proxy (token)), destroyFlag);
}

zxdg_decoration_manager_v1* bindZxdgDecorationManagerV1 (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) zxdgDecorationManagerV1Interface.version);
    return static_cast<zxdg_decoration_manager_v1*> (registryBind (registry, name, &zxdgDecorationManagerV1Interface, bindVersion));
}

void zxdgDecorationManagerV1Destroy (zxdg_decoration_manager_v1* manager)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager), toUnderlyingType (ZxdgDecorationManagerV1Request::destroy), nullptr, getVersion (proxy (manager)), destroyFlag);
}

zxdg_toplevel_decoration_v1* zxdgDecorationManagerV1GetToplevelDecoration (zxdg_decoration_manager_v1* manager, xdg_toplevel* toplevel)
{
    return object<zxdg_toplevel_decoration_v1> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager),
                                                                                                    toUnderlyingType (ZxdgDecorationManagerV1Request::getToplevelDecoration),
                                                                                                    &zxdgToplevelDecorationV1Interface,
                                                                                                    getVersion (proxy (manager)),
                                                                                                    0,
                                                                                                    nullptr,
                                                                                                    toplevel));
}

int zxdgToplevelDecorationV1AddListener (zxdg_toplevel_decoration_v1* decoration, const zxdg_toplevel_decoration_v1_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (decoration),
                                                             reinterpret_cast<void (**) (void)> (const_cast<zxdg_toplevel_decoration_v1_listener*> (listener)),
                                                             data);
}

void zxdgToplevelDecorationV1SetMode (zxdg_toplevel_decoration_v1* decoration, uint32_t mode)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (decoration), toUnderlyingType (ZxdgToplevelDecorationV1Request::setMode), nullptr, getVersion (proxy (decoration)), 0, mode);
}

void zxdgToplevelDecorationV1Destroy (zxdg_toplevel_decoration_v1* decoration)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (decoration), toUnderlyingType (ZxdgToplevelDecorationV1Request::destroy), nullptr, getVersion (proxy (decoration)), destroyFlag);
}

wp_viewporter* bindWpViewporter (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) wpViewporterInterface.version);
    return static_cast<wp_viewporter*> (registryBind (registry, name, &wpViewporterInterface, bindVersion));
}

void wpViewporterDestroy (wp_viewporter* viewporter)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (viewporter), toUnderlyingType (WpViewporterRequest::destroy), nullptr, getVersion (proxy (viewporter)), destroyFlag);
}

wp_viewport* wpViewporterGetViewport (wp_viewporter* viewporter, wl_surface* surface)
{
    return object<wp_viewport> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (viewporter),
                                                                                   toUnderlyingType (WpViewporterRequest::getViewport),
                                                                                   &wpViewportInterface,
                                                                                   getVersion (proxy (viewporter)),
                                                                                   0,
                                                                                   nullptr,
                                                                                   surface));
}

void wpViewportSetDestination (wp_viewport* viewport, int32_t width, int32_t height)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (viewport), toUnderlyingType (WpViewportRequest::setDestination), nullptr, getVersion (proxy (viewport)), 0, width, height);
}

void wpViewportDestroy (wp_viewport* viewport)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (viewport), toUnderlyingType (WpViewportRequest::destroy), nullptr, getVersion (proxy (viewport)), destroyFlag);
}

wp_fractional_scale_manager_v1* bindWpFractionalScaleManagerV1 (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) wpFractionalScaleManagerV1Interface.version);
    return static_cast<wp_fractional_scale_manager_v1*> (registryBind (registry, name, &wpFractionalScaleManagerV1Interface, bindVersion));
}

void wpFractionalScaleManagerV1Destroy (wp_fractional_scale_manager_v1* manager)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager), toUnderlyingType (WpFractionalScaleManagerV1Request::destroy), nullptr, getVersion (proxy (manager)), destroyFlag);
}

wp_fractional_scale_v1* wpFractionalScaleManagerV1GetFractionalScale (wp_fractional_scale_manager_v1* manager, wl_surface* surface)
{
    return object<wp_fractional_scale_v1> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (manager),
                                                                                              toUnderlyingType (WpFractionalScaleManagerV1Request::getFractionalScale),
                                                                                              &wpFractionalScaleV1Interface,
                                                                                              getVersion (proxy (manager)),
                                                                                              0,
                                                                                              nullptr,
                                                                                              surface));
}

int wpFractionalScaleV1AddListener (wp_fractional_scale_v1* fractionalScale, const wp_fractional_scale_v1_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (fractionalScale),
                                                             reinterpret_cast<void (**) (void)> (const_cast<wp_fractional_scale_v1_listener*> (listener)),
                                                             data);
}

void wpFractionalScaleV1Destroy (wp_fractional_scale_v1* fractionalScale)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (fractionalScale), toUnderlyingType (WpFractionalScaleV1Request::destroy), nullptr, getVersion (proxy (fractionalScale)), destroyFlag);
}

wp_alpha_modifier_v1* bindWpAlphaModifierV1 (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) wpAlphaModifierV1Interface.version);
    return static_cast<wp_alpha_modifier_v1*> (registryBind (registry, name, &wpAlphaModifierV1Interface, bindVersion));
}

void wpAlphaModifierV1Destroy (wp_alpha_modifier_v1* modifier)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (modifier), toUnderlyingType (WpAlphaModifierV1Request::destroy), nullptr, getVersion (proxy (modifier)), destroyFlag);
}

wp_alpha_modifier_surface_v1* wpAlphaModifierV1GetSurface (wp_alpha_modifier_v1* modifier, wl_surface* surface)
{
    return object<wp_alpha_modifier_surface_v1> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (modifier),
                                                                                                    toUnderlyingType (WpAlphaModifierV1Request::getSurface),
                                                                                                    &wpAlphaModifierSurfaceV1Interface,
                                                                                                    getVersion (proxy (modifier)),
                                                                                                    0,
                                                                                                    nullptr,
                                                                                                    surface));
}

void wpAlphaModifierSurfaceV1Destroy (wp_alpha_modifier_surface_v1* modifierSurface)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (modifierSurface), toUnderlyingType (WpAlphaModifierSurfaceV1Request::destroy), nullptr, getVersion (proxy (modifierSurface)), destroyFlag);
}

void wpAlphaModifierSurfaceV1SetMultiplier (wp_alpha_modifier_surface_v1* modifierSurface, uint32_t factor)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (modifierSurface), toUnderlyingType (WpAlphaModifierSurfaceV1Request::setMultiplier), nullptr, getVersion (proxy (modifierSurface)), 0, factor);
}

zxdg_exporter_v2* bindZxdgExporterV2 (wl_registry* registry, uint32_t name, uint32_t version)
{
    const auto bindVersion = jmin (version, (uint32_t) zxdgExporterV2Interface.version);
    return static_cast<zxdg_exporter_v2*> (registryBind (registry, name, &zxdgExporterV2Interface, bindVersion));
}

void zxdgExporterV2Destroy (zxdg_exporter_v2* exporter)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (exporter), toUnderlyingType (ZxdgExporterV2Request::destroy), nullptr, getVersion (proxy (exporter)), destroyFlag);
}

zxdg_exported_v2* zxdgExporterV2ExportToplevel (zxdg_exporter_v2* exporter, wl_surface* surface)
{
    return object<zxdg_exported_v2> (WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (exporter),
                                                                                        toUnderlyingType (ZxdgExporterV2Request::exportToplevel),
                                                                                        &zxdgExportedV2Interface,
                                                                                        getVersion (proxy (exporter)),
                                                                                        0,
                                                                                        nullptr,
                                                                                        surface));
}

int zxdgExportedV2AddListener (zxdg_exported_v2* exported, const zxdg_exported_v2_listener* listener, void* data)
{
    return WaylandSymbols::getInstance()->wlProxyAddListener (proxy (exported),
                                                             reinterpret_cast<void (**) (void)> (const_cast<zxdg_exported_v2_listener*> (listener)),
                                                             data);
}

void zxdgExportedV2Destroy (zxdg_exported_v2* exported)
{
    WaylandSymbols::getInstance()->wlProxyMarshalFlags (proxy (exported), toUnderlyingType (ZxdgExportedV2Request::destroy), nullptr, getVersion (proxy (exported)), destroyFlag);
}

} // namespace juce::WaylandProtocol
