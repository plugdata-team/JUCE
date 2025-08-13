/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
 */

namespace juce
{

namespace WaylandSymbolHelpers
{

template <typename FuncPtr>
struct SymbolBinding
{
    FuncPtr& func;
    const char* name;
};

template <typename FuncPtr>
SymbolBinding<FuncPtr> makeSymbolBinding (FuncPtr& func, const char* name)
{
    return { func, name };
}

template <typename FuncPtr>
bool loadSymbols (DynamicLibrary& lib, SymbolBinding<FuncPtr> binding)
{
    if (auto* func = lib.getFunction (binding.name))
    {
        binding.func = reinterpret_cast<FuncPtr> (func);
        return true;
    }
    
    return false;
}

template <typename FuncPtr, typename... Args>
bool loadSymbols (DynamicLibrary& lib, SymbolBinding<FuncPtr> binding, Args... args)
{
    return loadSymbols (lib, binding) && loadSymbols (lib, args...);
}
}


//==============================================================================
bool WaylandSymbols::loadAllSymbols()
{
    using namespace WaylandSymbolHelpers;
    
    JUCE_DEFINE_INLINE_FUNCTION (wl_display_get_registry, displayGetRegistry)
    JUCE_DEFINE_INLINE_FUNCTION (wl_registry_add_listener, registryAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_registry_bind, registryBind)
    JUCE_DEFINE_INLINE_FUNCTION (wl_registry_destroy, registryDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_compositor_create_surface, compositorCreateSurface)
    JUCE_DEFINE_INLINE_FUNCTION (wl_compositor_create_region, compositorCreateRegion)
    JUCE_DEFINE_INLINE_FUNCTION (wl_compositor_destroy, compositorDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subcompositor_destroy, subcompositorDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subcompositor_get_subsurface, subcompositorGetSubsurface)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_destroy, surfaceDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_attach, surfaceAttach)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_damage, surfaceDamage)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_damage_buffer, surfaceDamageBuffer)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_frame, surfaceFrame)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_set_input_region, surfaceSetInputRegion)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_set_opaque_region, surfaceSetOpaqueRegion)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_commit, surfaceCommit)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_set_buffer_scale, surfaceSetBufferScale)
    JUCE_DEFINE_INLINE_FUNCTION (wl_surface_add_listener, surfaceAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subsurface_destroy, subsurfaceDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subsurface_set_position, subsurfaceSetPosition)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subsurface_place_above, subsurfacePlaceAbove)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subsurface_place_below, subsurfacePlaceBelow)
    JUCE_DEFINE_INLINE_FUNCTION (wl_subsurface_set_desync, subsurfaceSetDesync)
    JUCE_DEFINE_INLINE_FUNCTION (wl_seat_add_listener, seatAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_seat_get_pointer, seatGetPointer)
    JUCE_DEFINE_INLINE_FUNCTION (wl_seat_get_keyboard, seatGetKeyboard)
    JUCE_DEFINE_INLINE_FUNCTION (wl_seat_get_touch, seatGetTouch)
    JUCE_DEFINE_INLINE_FUNCTION (wl_seat_destroy, seatDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_pointer_add_listener, pointerAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_pointer_set_cursor, pointerSetCursor)
    JUCE_DEFINE_INLINE_FUNCTION (wl_pointer_release, pointerRelease)
    JUCE_DEFINE_INLINE_FUNCTION (wl_pointer_destroy, pointerDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_keyboard_add_listener, keyboardAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_touch_add_listener, touchAddListener);  
  	
    JUCE_DEFINE_INLINE_FUNCTION (wl_keyboard_release, keyboardRelease)
    JUCE_DEFINE_INLINE_FUNCTION (wl_keyboard_destroy, keyboardDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_manager_create_data_source, dataDeviceManagerCreateDataSource)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_manager_get_data_device, dataDeviceManagerGetDataDevice)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_manager_destroy, dataDeviceManagerDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_add_listener, dataDeviceAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_start_drag, dataDeviceStartDrag)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_set_selection, dataDeviceSetSelection)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_release, dataDeviceRelease)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_device_destroy, dataDeviceDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_source_offer, dataSourceOffer)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_source_add_listener, dataSourceAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_source_set_actions, dataSourceSetActions)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_offer_accept, dataOfferAccept)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_offer_receive, dataOfferReceive)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_offer_finish, dataOfferFinish)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_offer_set_actions, dataOfferSetActions)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_offer_add_listener, dataOfferAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_data_offer_destroy, dataOfferDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_shm_create_pool, shmCreatePool)
    JUCE_DEFINE_INLINE_FUNCTION (wl_shm_destroy, shmDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_shm_pool_create_buffer, shmPoolCreateBuffer)
    JUCE_DEFINE_INLINE_FUNCTION (wl_shm_pool_destroy, shmPoolDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_buffer_destroy, bufferDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_callback_add_listener, callbackAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_callback_destroy, callbackDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_region_destroy, regionDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_region_add, regionAdd)
    JUCE_DEFINE_INLINE_FUNCTION (wl_output_add_listener, outputAddListener)
    JUCE_DEFINE_INLINE_FUNCTION (wl_output_destroy, outputDestroy)
    JUCE_DEFINE_INLINE_FUNCTION (wl_fixed_to_int, fixedToInt);
    JUCE_DEFINE_INLINE_FUNCTION (wl_fixed_to_double, fixedToDouble);
    
    if (! loadSymbols (waylandClientLib,
                       makeSymbolBinding (displayConnect,              "wl_display_connect"),
                       makeSymbolBinding (displayDisconnect,           "wl_display_disconnect"),
                       makeSymbolBinding (displayGetFd,                "wl_display_get_fd"),
                       makeSymbolBinding (displayDispatch,             "wl_display_dispatch"),
                       makeSymbolBinding (displayDispatchPending,      "wl_display_dispatch_pending"),
                       makeSymbolBinding (displayFlush,                "wl_display_flush"),
                       makeSymbolBinding (displayRoundtrip,            "wl_display_roundtrip"),
                       makeSymbolBinding (displayPrepareRead,          "wl_display_prepare_read"),
                       makeSymbolBinding (displayCancelRead,           "wl_display_cancel_read"),
                       makeSymbolBinding (displayReadEvents,           "wl_display_read_events"),
                       makeSymbolBinding (displayConnectToFd,          "wl_display_connect_to_fd"),
                       makeSymbolBinding (displayCreateQueue,          "wl_display_create_queue"),
                       makeSymbolBinding (displayDispatchQueue,        "wl_display_dispatch_queue"),
                       makeSymbolBinding (displayDispatchQueuePending, "wl_display_dispatch_queue_pending"),
                       makeSymbolBinding (displayGetError,             "wl_display_get_error"),
                       makeSymbolBinding (displayGetProtocolError,     "wl_display_get_protocol_error"),
                       makeSymbolBinding (displayPrepareReadQueue,     "wl_display_prepare_read_queue"),
                       makeSymbolBinding (displayRoundtripQueue,       "wl_display_roundtrip_queue"),
                       makeSymbolBinding (arrayAdd,                    "wl_array_add"),
                       makeSymbolBinding (arrayCopy,                   "wl_array_copy"),
                       makeSymbolBinding (arrayInit,                   "wl_array_init"),
                       makeSymbolBinding (arrayRelease,                "wl_array_release"),
                       makeSymbolBinding (eventQueueDestroy,           "wl_event_queue_destroy"),
                       makeSymbolBinding (listEmpty,                   "wl_list_empty"),
                       makeSymbolBinding (listInit,                    "wl_list_init"),
                       makeSymbolBinding (listInsert,                  "wl_list_insert"),
                       makeSymbolBinding (listInsertList,              "wl_list_insert_list"),
                       makeSymbolBinding (listLength,                  "wl_list_length"),
                       makeSymbolBinding (listRemove,                  "wl_list_remove"),
                       makeSymbolBinding (logSetHandlerClient,         "wl_log_set_handler_client"),
                       
                       makeSymbolBinding (proxyAddDispatcher,                   "wl_proxy_add_dispatcher"),
                       makeSymbolBinding (proxyAddListener,                     "wl_proxy_add_listener"),
                       makeSymbolBinding (proxyCreate,                          "wl_proxy_create"),
                       makeSymbolBinding (proxyCreateWrapper,                   "wl_proxy_create_wrapper"),
                       makeSymbolBinding (proxyDestroy,                         "wl_proxy_destroy"),
                       makeSymbolBinding (proxyGetClass,                        "wl_proxy_get_class"),
                       makeSymbolBinding (proxyGetId,                           "wl_proxy_get_id"),
                       makeSymbolBinding (proxyGetListener,                     "wl_proxy_get_listener"),
                       makeSymbolBinding (proxyGetTag,                          "wl_proxy_get_tag"),
                       makeSymbolBinding (proxyGetUserData,                     "wl_proxy_get_user_data"),
                       makeSymbolBinding (proxySetUserData,                     "wl_proxy_set_user_data"),
                       makeSymbolBinding (proxyGetVersion,                      "wl_proxy_get_version"),
                       makeSymbolBinding (proxyMarshalArray,                    "wl_proxy_marshal_array"),
                       makeSymbolBinding (proxyMarshalArrayConstructor,         "wl_proxy_marshal_array_constructor"),
                       makeSymbolBinding (proxyMarshalArrayConstructorVersioned,"wl_proxy_marshal_array_constructor_versioned"),
                       makeSymbolBinding (proxyMarshalArrayFlags,               "wl_proxy_marshal_array_flags"),
                       makeSymbolBinding (proxySetQueue,                        "wl_proxy_set_queue"),
                       makeSymbolBinding (proxySetTag,                          "wl_proxy_set_tag"),
                       makeSymbolBinding (proxyWrapperDestroy,                  "wl_proxy_wrapper_destroy"),
                       
                       makeSymbolBinding (displayInterface,          "wl_display_interface"),
                       makeSymbolBinding (registryInterface,         "wl_registry_interface"),
                       makeSymbolBinding (callbackInterface,         "wl_callback_interface"),
                       makeSymbolBinding (compositorInterface,       "wl_compositor_interface"),
                       makeSymbolBinding (shmPoolInterface,          "wl_shm_pool_interface"),
                       makeSymbolBinding (shmInterface,              "wl_shm_interface"),
                       makeSymbolBinding (bufferInterface,           "wl_buffer_interface"),
                       makeSymbolBinding (dataOfferInterface,        "wl_data_offer_interface"),
                       makeSymbolBinding (dataSourceInterface,       "wl_data_source_interface"),
                       makeSymbolBinding (dataDeviceInterface,       "wl_data_device_interface"),
                       makeSymbolBinding (dataDeviceManagerInterface,"wl_data_device_manager_interface"),
                       makeSymbolBinding (shellInterface,            "wl_shell_interface"),
                       makeSymbolBinding (shellSurfaceInterface,     "wl_shell_surface_interface"),
                       makeSymbolBinding (surfaceInterface,          "wl_surface_interface"),
                       makeSymbolBinding (seatInterface,             "wl_seat_interface"),
                       makeSymbolBinding (pointerInterface,          "wl_pointer_interface"),
                       makeSymbolBinding (keyboardInterface,         "wl_keyboard_interface"),
                       makeSymbolBinding (touchInterface,            "wl_touch_interface"),
                       makeSymbolBinding (outputInterface,           "wl_output_interface"),
                       makeSymbolBinding (regionInterface,           "wl_region_interface"),
                       makeSymbolBinding (subcompositorInterface,    "wl_subcompositor_interface"),
                       makeSymbolBinding (subsurfaceInterface,       "wl_subsurface_interface")
                       ))
        return false;

    // Can't bind with makeSymbolBinding because of the variadic args
    proxyMarshalFlags = (ProxyMarshalFlagsFn) waylandClientLib.getFunction ("wl_proxy_marshal_flags");
    
    if (! proxyMarshalFlags)
        return false;
    
    // wl_cursor functions (libwayland-cursor)
    if (! loadSymbols (waylandCursorLib,
                       makeSymbolBinding (cursorThemeLoad,        "wl_cursor_theme_load"),
                       makeSymbolBinding (cursorThemeDestroy,     "wl_cursor_theme_destroy"),
                       makeSymbolBinding (cursorThemeGetCursor,   "wl_cursor_theme_get_cursor"),
                       makeSymbolBinding (cursorImageGetBuffer,   "wl_cursor_image_get_buffer")
                       ))
        return false;
    
    if (! loadSymbols (xkbLib,
                       makeSymbolBinding (xkbStateUpdateMask,       "xkb_state_update_mask"),
                       makeSymbolBinding (xkbKeymapUnref,           "xkb_keymap_unref"),
                       makeSymbolBinding (xkbKeymapNewFromString,   "xkb_keymap_new_from_string"),
                       makeSymbolBinding (xkbStateUnref,            "xkb_state_unref"),
                       makeSymbolBinding (xkbStateNew,              "xkb_state_new"),
                       makeSymbolBinding (xkbStateKeyGetOneSym,     "xkb_state_key_get_one_sym"),
                       makeSymbolBinding (xkbStateKeyGetUtf8,       "xkb_state_key_get_utf8"),
                       makeSymbolBinding (xkbStateModNameIsActive,  "xkb_state_mod_name_is_active"),
                       makeSymbolBinding (xkbContextNew,            "xkb_context_new")
                       ))
        return false;
    
    if (! loadSymbols (libdecorLib,
                       makeSymbolBinding (decorConfigurationGetWindowState,   "libdecor_configuration_get_window_state"),
                       makeSymbolBinding (decorConfigurationGetContentSize,   "libdecor_configuration_get_content_size"),
                       makeSymbolBinding (decorStateNew,                      "libdecor_state_new"),
                       makeSymbolBinding (decorFrameCommit,                   "libdecor_frame_commit"),
                       makeSymbolBinding (decorStateFree,                     "libdecor_state_free"),
                       makeSymbolBinding (decorNew,                           "libdecor_new"),
                       makeSymbolBinding (decorUnref,                         "libdecor_unref"),
                       makeSymbolBinding (decorDecorate,                      "libdecor_decorate"),
                       makeSymbolBinding (decorFrameSetCapabilities,          "libdecor_frame_set_capabilities"),
                       makeSymbolBinding (decorFrameSetVisibility,            "libdecor_frame_set_visibility"),
                       makeSymbolBinding (decorFrameSetTitle,                 "libdecor_frame_set_title"),
                       makeSymbolBinding (decorFrameSetMinContentSize,        "libdecor_frame_set_min_content_size"),
                       makeSymbolBinding (decorFrameSetMaxContentSize,        "libdecor_frame_set_max_content_size"),
                       makeSymbolBinding (decorFrameMap,                      "libdecor_frame_map"),
                       makeSymbolBinding (decorFrameUnref,                    "libdecor_frame_unref"),
                       makeSymbolBinding (decorFrameSetMinimized,             "libdecor_frame_set_minimized"),
                       makeSymbolBinding (decorFrameSetMaximized,             "libdecor_frame_set_maximized"),
                       makeSymbolBinding (decorFrameUnsetMaximized,           "libdecor_frame_unset_maximized"),
                       makeSymbolBinding (decorFrameIsFloating,               "libdecor_frame_is_floating"),
                       makeSymbolBinding (decorFrameIsFloating,               "libdecor_frame_unset_maximized"),
                       makeSymbolBinding (decorFrameResize,                   "libdecor_frame_resize"),
                       makeSymbolBinding (decorFrameMove,                     "libdecor_frame_move")
                       )) {
        std::cerr << "Falling back to XWayland mode because libdecor was not found" << std::endl;
        return false;
    }
    
    return true;
}

} // namespace juce
