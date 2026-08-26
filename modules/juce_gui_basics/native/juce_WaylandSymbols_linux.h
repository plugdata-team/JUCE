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

#define JUCE_GENERATE_WAYLAND_FUNCTION(functionName, objectName, args, returnType) \
    using functionName      = returnType (*) args; \
    functionName objectName = nullptr;

class JUCE_API  WaylandSymbols
{
public:
    bool loadAllSymbols();

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayConnect, wlDisplayConnect,
                                    (const char*),
                                    wl_display*)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayDisconnect, wlDisplayDisconnect,
                                    (wl_display*),
                                    void)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayGetFd, wlDisplayGetFd,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayDispatchPending, wlDisplayDispatchPending,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayPrepareRead, wlDisplayPrepareRead,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayReadEvents, wlDisplayReadEvents,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayCancelRead, wlDisplayCancelRead,
                                    (wl_display*),
                                    void)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayFlush, wlDisplayFlush,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayRoundtrip, wlDisplayRoundtrip,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayGetError, wlDisplayGetError,
                                    (wl_display*),
                                    int)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlDisplayGetProtocolError, wlDisplayGetProtocolError,
                                    (wl_display*, const wl_interface**, uint32_t*),
                                    uint32_t)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlProxyMarshalFlags, wlProxyMarshalFlags,
                                    (wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, ...),
                                    wl_proxy*)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlProxyGetVersion, wlProxyGetVersion,
                                    (wl_proxy*),
                                    uint32_t)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlProxyDestroy, wlProxyDestroy,
                                    (wl_proxy*),
                                    void)

    JUCE_GENERATE_WAYLAND_FUNCTION (WlProxyAddListener, wlProxyAddListener,
                                    (wl_proxy*, void (**)(void), void*),
                                    int)

    const wl_interface* wlBufferInterface = nullptr;
    const wl_interface* wlCallbackInterface = nullptr;
    const wl_interface* wlCompositorInterface = nullptr;
    const wl_interface* wlDataDeviceInterface = nullptr;
    const wl_interface* wlDataDeviceManagerInterface = nullptr;
    const wl_interface* wlDataOfferInterface = nullptr;
    const wl_interface* wlDataSourceInterface = nullptr;
    const wl_interface* wlKeyboardInterface = nullptr;
    const wl_interface* wlOutputInterface = nullptr;
    const wl_interface* wlPointerInterface = nullptr;
    const wl_interface* wlRegionInterface = nullptr;
    const wl_interface* wlRegistryInterface = nullptr;
    const wl_interface* wlSeatInterface = nullptr;
    const wl_interface* wlShmInterface = nullptr;
    const wl_interface* wlShmPoolInterface = nullptr;
    const wl_interface* wlSubcompositorInterface = nullptr;
    const wl_interface* wlSubsurfaceInterface = nullptr;
    const wl_interface* wlSurfaceInterface = nullptr;
    const wl_interface* wlTouchInterface = nullptr;

    JUCE_DECLARE_SINGLETON_INLINE (WaylandSymbols, false)

private:
    WaylandSymbols() = default;

    ~WaylandSymbols()
    {
        clearSingletonInstance();
    }

    DynamicLibrary waylandClient { "libwayland-client.so.0" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaylandSymbols)
};

#undef JUCE_GENERATE_WAYLAND_FUNCTION

} // namespace juce
