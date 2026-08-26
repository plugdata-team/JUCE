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

//==============================================================================
bool WaylandSymbols::loadAllSymbols()
{
    if (! waylandClient.loadInto (wlDisplayConnect,             "wl_display_connect")
        || ! waylandClient.loadInto (wlDisplayDisconnect,       "wl_display_disconnect")
        || ! waylandClient.loadInto (wlDisplayGetFd,            "wl_display_get_fd")
        || ! waylandClient.loadInto (wlDisplayDispatchPending,  "wl_display_dispatch_pending")
        || ! waylandClient.loadInto (wlDisplayPrepareRead,      "wl_display_prepare_read")
        || ! waylandClient.loadInto (wlDisplayReadEvents,       "wl_display_read_events")
        || ! waylandClient.loadInto (wlDisplayCancelRead,       "wl_display_cancel_read")
        || ! waylandClient.loadInto (wlDisplayFlush,            "wl_display_flush")
        || ! waylandClient.loadInto (wlDisplayRoundtrip,        "wl_display_roundtrip")
        || ! waylandClient.loadInto (wlDisplayGetError,         "wl_display_get_error")
        || ! waylandClient.loadInto (wlDisplayGetProtocolError, "wl_display_get_protocol_error")
        || ! waylandClient.loadInto (wlProxyMarshalFlags,       "wl_proxy_marshal_flags")
        || ! waylandClient.loadInto (wlProxyGetVersion,         "wl_proxy_get_version")
        || ! waylandClient.loadInto (wlProxyDestroy,            "wl_proxy_destroy")
        || ! waylandClient.loadInto (wlProxyAddListener,        "wl_proxy_add_listener"))
    {
        return false;
    }

    return waylandClient.loadInto (wlBufferInterface,            "wl_buffer_interface")
        && waylandClient.loadInto (wlCallbackInterface,          "wl_callback_interface")
        && waylandClient.loadInto (wlCompositorInterface,        "wl_compositor_interface")
        && waylandClient.loadInto (wlDataDeviceInterface,        "wl_data_device_interface")
        && waylandClient.loadInto (wlDataDeviceManagerInterface, "wl_data_device_manager_interface")
        && waylandClient.loadInto (wlDataOfferInterface,         "wl_data_offer_interface")
        && waylandClient.loadInto (wlDataSourceInterface,        "wl_data_source_interface")
        && waylandClient.loadInto (wlKeyboardInterface,          "wl_keyboard_interface")
        && waylandClient.loadInto (wlOutputInterface,            "wl_output_interface")
        && waylandClient.loadInto (wlPointerInterface,           "wl_pointer_interface")
        && waylandClient.loadInto (wlRegionInterface,            "wl_region_interface")
        && waylandClient.loadInto (wlRegistryInterface,          "wl_registry_interface")
        && waylandClient.loadInto (wlSeatInterface,              "wl_seat_interface")
        && waylandClient.loadInto (wlShmInterface,               "wl_shm_interface")
        && waylandClient.loadInto (wlShmPoolInterface,           "wl_shm_pool_interface")
        && waylandClient.loadInto (wlSubcompositorInterface,     "wl_subcompositor_interface")
        && waylandClient.loadInto (wlSubsurfaceInterface,        "wl_subsurface_interface")
        && waylandClient.loadInto (wlSurfaceInterface,           "wl_surface_interface")
        && waylandClient.loadInto (wlTouchInterface,             "wl_touch_interface");
}

} // namespace juce
