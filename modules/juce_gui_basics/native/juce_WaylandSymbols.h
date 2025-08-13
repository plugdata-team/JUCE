/*
 // Copyright (c) 2025 Timothy Schoen
 // Distributed under the GPLv3 license
*/

#define JUCE_DECLARE_INLINE_FUNCTION(functionName, objectName, args, returnType) \
    returnType (*objectName)args  = nullptr;

#define JUCE_DEFINE_INLINE_FUNCTION(functionName, objectName) \
   objectName = &functionName;

struct wl_display;
struct wl_registry;
struct wl_surface;
struct wl_subsurface;
struct wl_compositor;
struct wl_subcompositor;
struct wl_pointer;
struct wl_keyboard;
struct wl_proxy;
struct wl_event_queue;
struct wl_registry;
struct wl_registry_listener;
struct wl_compositor;
struct wl_region;
struct wl_surface_listener;
struct wl_seat;
struct wl_seat_listener;
struct wl_touch;
struct wl_touch_listener;
struct wl_pointer_listener;
struct wl_keyboard_listener;
struct wl_data_device_manager;
struct wl_data_device;
struct wl_data_device_listener;
struct wl_data_source;
struct wl_data_source_listener;
struct wl_data_offer;
struct wl_data_offer_listener;
struct wl_shm;
struct wl_shm_pool;
struct wl_buffer;
struct wl_callback;
struct wl_callback_listener;
struct wl_output;
struct wl_output_listener;
struct wl_cursor_theme;
struct wl_cursor_image;
struct wl_cursor;

struct libdecor_configuration;
struct libdecor_frame;
struct libdecor_state;
struct libdecor_interface;
struct libdecor_frame_interface;
struct libdecor;

namespace juce
{

//==============================================================================
class JUCE_API  WaylandSymbols
{
  
public:
    //==============================================================================
    bool loadAllSymbols();
  
    //==============================================================================

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_connect, displayConnect,
                                         (const char*),
                                         wl_display*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_disconnect, displayDisconnect,
                                         (wl_display*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_get_fd, displayGetFd,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_dispatch, displayDispatch,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_dispatch_pending, displayDispatchPending,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_flush, displayFlush,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_roundtrip, displayRoundtrip,
                                         (wl_display*),
                                         int)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_prepare_read, displayPrepareRead,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_cancel_read, displayCancelRead,
                                         (wl_display*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_read_events, displayReadEvents,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_connect_to_fd, displayConnectToFd,
                                       (int),
                                       wl_display*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_create_queue, displayCreateQueue,
                                         (wl_display*),
                                         wl_event_queue*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_dispatch_queue, displayDispatchQueue,
                                         (wl_display*, wl_event_queue*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_dispatch_queue_pending, displayDispatchQueuePending,
                                         (wl_display*, wl_event_queue*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_get_error, displayGetError,
                                         (wl_display*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_get_protocol_error, displayGetProtocolError,
                                         (wl_display*, const wl_interface**, uint32_t*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_prepare_read_queue, displayPrepareReadQueue,
                                         (wl_display*, wl_event_queue*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_display_roundtrip_queue, displayRoundtripQueue,
                                         (wl_display*, wl_event_queue*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_array_add, arrayAdd,
                                         (wl_array*, size_t),
                                         void*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_array_copy, arrayCopy,
                                         (wl_array*, wl_array*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_array_init, arrayInit,
                                         (wl_array*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_array_release, arrayRelease,
                                         (wl_array*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_event_queue_destroy, eventQueueDestroy,
                                         (wl_event_queue*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_list_empty, listEmpty,
                                         (const wl_list*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_list_init, listInit,
                                         (wl_list*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_list_insert, listInsert,
                                         (wl_list*, wl_list*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_list_insert_list, listInsertList,
                                         (wl_list*, wl_list*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_list_length, listLength,
                                         (const wl_list*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_list_remove, listRemove,
                                         (wl_list*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_log_set_handler_client, logSetHandlerClient,
                                         (wl_log_func_t),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_add_dispatcher, proxyAddDispatcher,
                                         (wl_proxy*, wl_dispatcher_func_t, const void*, void*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_add_listener, proxyAddListener,
                                         (wl_proxy*, void (**)(void), void*),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_create, proxyCreate,
                                         (wl_proxy*, const wl_interface*),
                                         wl_proxy*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_create_wrapper, proxyCreateWrapper,
                                         (void*),
                                         void*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_destroy, proxyDestroy,
                                         (wl_proxy*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_get_class, proxyGetClass,
                                         (wl_proxy*),
                                         const char*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_get_id, proxyGetId,
                                         (wl_proxy*),
                                         uint32_t)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_get_listener, proxyGetListener,
                                         (wl_proxy*),
                                         const void*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_get_tag, proxyGetTag,
                                         (wl_proxy*),
                                         const void* const*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_get_user_data, proxyGetUserData,
                                         (wl_proxy*),
                                         void*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_set_user_data, proxySetUserData,
                                         (wl_proxy*, void*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_get_version, proxyGetVersion,
                                         (wl_proxy*),
                                         uint32_t)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_marshal_array, proxyMarshalArray,
                                         (wl_proxy*),
                                         uint32_t)
                      
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_marshal_array_constructor, proxyMarshalArrayConstructor,
                                         (wl_proxy*, uint32_t, const wl_interface*, uint32_t, union wl_argument*),
                                         wl_proxy*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_marshal_array_constructor_versioned, proxyMarshalArrayConstructorVersioned,
                                         (wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, union wl_argument*),
                                         wl_proxy*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_marshal_array_flags, proxyMarshalArrayFlags,
                                         (wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, union wl_argument*),
                                         wl_proxy*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_set_queue, proxySetQueue,
                                         (wl_proxy*, wl_event_queue*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_set_tag, proxySetTag,
                                         (wl_proxy*, const char* const*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_proxy_wrapper_destroy, proxyWrapperDestroy,
                                         (void*),
                                         void)

    JUCE_DECLARE_INLINE_FUNCTION (wl_display_get_registry, displayGetRegistry,
                                         (wl_display*),
                                         wl_registry*)

    // wl_registry
    JUCE_DECLARE_INLINE_FUNCTION (wl_registry_add_listener, registryAddListener,
                                         (wl_registry*, const wl_registry_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_registry_bind, registryBind,
                                         (wl_registry*, uint32_t, const wl_interface*, uint32_t),
                                         void*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_registry_destroy, registryDestroy,
                                         (wl_registry*),
                                         void)
   
    // wl_compositor
    JUCE_DECLARE_INLINE_FUNCTION (wl_compositor_create_surface, compositorCreateSurface,
                                         (wl_compositor*),
                                         wl_surface*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_compositor_create_region, compositorCreateRegion,
                                         (wl_compositor*),
                                         wl_region*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_compositor_destroy, compositorDestroy,
                                         (wl_compositor*),
                                         void)

    // wl_subcompositor
    JUCE_DECLARE_INLINE_FUNCTION (wl_subcompositor_destroy, subcompositorDestroy,
                                         (wl_subcompositor*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_subcompositor_get_subsurface, subcompositorGetSubsurface,
                                         (wl_subcompositor*, wl_surface*, wl_surface*),
                                         wl_subsurface*)
    
    // wl_surface
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_destroy, surfaceDestroy,
                                         (wl_surface*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_attach, surfaceAttach,
                                         (wl_surface*, wl_buffer*, int32_t, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_damage, surfaceDamage,
                                         (wl_surface*, int32_t, int32_t, int32_t, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_damage_buffer, surfaceDamageBuffer,
                                         (wl_surface*, int32_t, int32_t, int32_t, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_frame, surfaceFrame,
                                         (wl_surface*),
                                         wl_callback*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_set_input_region, surfaceSetInputRegion,
                                         (wl_surface*, wl_region*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_set_opaque_region, surfaceSetOpaqueRegion,
                                      (wl_surface*, wl_region*),
                                      void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_commit, surfaceCommit,
                                         (wl_surface*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_set_buffer_scale, surfaceSetBufferScale,
                                         (wl_surface*, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_surface_add_listener, surfaceAddListener,
                                         (wl_surface*, const wl_surface_listener*, void*),
                                         int)

    // wl_subsurface
    JUCE_DECLARE_INLINE_FUNCTION (wl_subsurface_destroy, subsurfaceDestroy,
                                         (wl_subsurface*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_subsurface_set_position, subsurfaceSetPosition,
                                         (wl_subsurface*, int32_t, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_subsurface_place_above, subsurfacePlaceAbove,
                                         (wl_subsurface*, wl_surface*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_subsurface_place_below, subsurfacePlaceBelow,
                                         (wl_subsurface*, wl_surface*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_subsurface_set_desync, subsurfaceSetDesync,
                                         (wl_subsurface*),
                                         void)
    
    // wl_seat
    JUCE_DECLARE_INLINE_FUNCTION (wl_seat_add_listener, seatAddListener,
                                         (wl_seat*, const wl_seat_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_seat_get_pointer, seatGetPointer,
                                         (wl_seat*),
                                         wl_pointer*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_seat_get_keyboard, seatGetKeyboard,
                                         (wl_seat*),
                                         wl_keyboard*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_seat_get_touch, seatGetTouch,
                                         (wl_seat*),
                                         wl_touch*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_seat_destroy, seatDestroy,
                                         (wl_seat*),
                                         void)

    // wl_pointer
    JUCE_DECLARE_INLINE_FUNCTION (wl_pointer_add_listener, pointerAddListener,
                                         (wl_pointer*, const wl_pointer_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_pointer_set_cursor, pointerSetCursor,
                                         (wl_pointer*, uint32_t, wl_surface*, int32_t, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_pointer_release, pointerRelease,
                                         (wl_pointer*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_pointer_destroy, pointerDestroy,
                                         (wl_pointer*),
                                         void)
    
    // wl_keyboard
    JUCE_DECLARE_INLINE_FUNCTION (wl_keyboard_add_listener, keyboardAddListener,
                                         (wl_keyboard*, const wl_keyboard_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_keyboard_release, keyboardRelease,
                                         (wl_keyboard*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_keyboard_destroy, keyboardDestroy,
                                         (wl_keyboard*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION(wl_touch_add_listener, touchAddListener, (wl_touch*, const wl_touch_listener*, void*), int)

    // wl_data_device_manager
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_manager_create_data_source, dataDeviceManagerCreateDataSource,
                                         (wl_data_device_manager*),
                                         wl_data_source*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_manager_get_data_device, dataDeviceManagerGetDataDevice,
                                         (wl_data_device_manager*, wl_seat*),
                                         wl_data_device*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_manager_destroy, dataDeviceManagerDestroy,
                                         (wl_data_device_manager*),
                                         void)
    
    // wl_data_device
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_add_listener, dataDeviceAddListener,
                                         (wl_data_device*, const wl_data_device_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_start_drag, dataDeviceStartDrag,
                                         (wl_data_device*, wl_data_source*, wl_surface*, wl_surface*, uint32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_set_selection, dataDeviceSetSelection,
                                         (wl_data_device*, wl_data_source*, uint32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_release, dataDeviceRelease,
                                         (wl_data_device*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_device_destroy, dataDeviceDestroy,
                                         (wl_data_device*),
                                         void)
    
    // wl_data_source
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_source_offer, dataSourceOffer,
                                         (wl_data_source*, const char*),
                                         void)

    JUCE_DECLARE_INLINE_FUNCTION (wl_data_source_add_listener, dataSourceAddListener,
                                         (wl_data_source*, const wl_data_source_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_source_set_actions, dataSourceSetActions,
                                         (wl_data_source*, uint32_t dnd_actions),
                                         void)
  
    // wl_data_offer
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_offer_accept, dataOfferAccept,
                                         (wl_data_offer*, uint32_t, const char*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_offer_receive, dataOfferReceive,
                                         (wl_data_offer*, const char*, int32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_offer_finish, dataOfferFinish,
                                         (wl_data_offer*),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_offer_set_actions, dataOfferSetActions,
                                         (wl_data_offer*, uint32_t, uint32_t),
                                         void)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_offer_add_listener, dataOfferAddListener,
                                         (wl_data_offer*, const wl_data_offer_listener*, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_data_offer_destroy, dataOfferDestroy,
                                         (wl_data_offer*),
                                         void)
    
    // wl_shm
    JUCE_DECLARE_INLINE_FUNCTION (wl_shm_create_pool, shmCreatePool,
                                         (wl_shm*, int, int32_t),
                                         wl_shm_pool*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_shm_destroy, shmDestroy,
                                         (wl_shm*),
                                         void)
    
    // wl_shm_pool
    JUCE_DECLARE_INLINE_FUNCTION (wl_shm_pool_create_buffer, shmPoolCreateBuffer,
                                         (wl_shm_pool*, int32_t, int32_t, int32_t, int32_t, uint32_t),
                                         wl_buffer*)
    JUCE_DECLARE_INLINE_FUNCTION (wl_shm_pool_destroy, shmPoolDestroy,
                                         (wl_shm_pool*),
                                         void)

    // wl_buffer functions
    JUCE_DECLARE_INLINE_FUNCTION (wl_buffer_destroy, bufferDestroy,
                                         (wl_buffer*),
                                         void)
    // wl_callback
    JUCE_DECLARE_INLINE_FUNCTION (wl_callback_add_listener, callbackAddListener,
                                         (wl_callback*, const wl_callback_listener* listener, void*),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_callback_destroy, callbackDestroy,
                                         (wl_callback*),
                                         void)

    // wl_region
    JUCE_DECLARE_INLINE_FUNCTION (wl_region_destroy, regionDestroy,
                                         (wl_region*),
                                         void)

    JUCE_DECLARE_INLINE_FUNCTION (wl_region_add, regionAdd,
                                         (wl_region*, int32_t, int32_t, int32_t, int32_t),
                                         void)
  
    // wl_output
    JUCE_DECLARE_INLINE_FUNCTION (wl_output_add_listener, outputAddListener,
                                         (wl_output*, const wl_output_listener*, void*),
                                         int)

    JUCE_DECLARE_INLINE_FUNCTION (wl_output_destroy, outputDestroy,
                                         (wl_output*),
                                         void)
  
    // wl_cursor (from libwayland-cursor)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_cursor_theme_load, cursorThemeLoad,
                                         (const char*, int, wl_shm*),
                                         wl_cursor_theme*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_cursor_theme_destroy, cursorThemeDestroy,
                                         (wl_cursor_theme*),
                                         void)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_cursor_theme_get_cursor, cursorThemeGetCursor,
                                         (wl_cursor_theme*, const char*),
                                         wl_cursor*)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (wl_cursor_image_get_buffer, cursorImageGetBuffer,
                                         (wl_cursor_image*),
                                         wl_buffer*)
    // wl_fixed
    JUCE_DECLARE_INLINE_FUNCTION (wl_fixed_to_int, fixedToInt,
                                         (wl_fixed_t),
                                         int)
    JUCE_DECLARE_INLINE_FUNCTION (wl_fixed_to_double, fixedToDouble,
                                         (wl_fixed_t),
                                         double)
  
    //==============================================================================

    // xkb
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_state_update_mask, xkbStateUpdateMask,
                                     (xkb_state*, xkb_mod_mask_t, xkb_mod_mask_t, xkb_mod_mask_t,
                                      xkb_layout_index_t, xkb_layout_index_t, xkb_layout_index_t),
                                     int)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_keymap_unref, xkbKeymapUnref,
                                         (xkb_keymap*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_keymap_new_from_string, xkbKeymapNewFromString,
                                         (xkb_context*, const char*, enum xkb_keymap_format, enum xkb_keymap_compile_flags),
                                         xkb_keymap*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_state_unref, xkbStateUnref,
                                         (xkb_state*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_state_new, xkbStateNew,
                                         (xkb_keymap*),
                                         xkb_state*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_state_key_get_one_sym, xkbStateKeyGetOneSym,
                                         (xkb_state*, xkb_keycode_t),
                                         xkb_keysym_t)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_state_key_get_utf8, xkbStateKeyGetUtf8,
                                         (xkb_state*, xkb_keycode_t, char*, size_t),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_state_mod_name_is_active, xkbStateModNameIsActive,
                                         (xkb_state *, const char*, xkb_state_component),
                                         int)
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (xkb_context_new, xkbContextNew,
                                         (xkb_context_flags),
                                          xkb_context*)
    // libdecor
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_configuration_get_window_state, decorConfigurationGetWindowState,
                                     (libdecor_configuration*, int*),
                                     int)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_configuration_get_content_size, decorConfigurationGetContentSize,
                                         (libdecor_configuration*, libdecor_frame*, int*, int*),
                                         bool)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_state_new, decorStateNew,
                                         (int, int),
                                        libdecor_state*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_commit, decorFrameCommit,
                                         (libdecor_frame*, libdecor_state*, libdecor_configuration*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_state_free, decorStateFree,
                                         (libdecor_state*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_new, decorNew,
                                         (wl_display*, const libdecor_interface*),
                                        libdecor*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_unref, decorUnref,
                                         (libdecor*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_decorate, decorDecorate,
                                         (libdecor*, wl_surface*, const libdecor_frame_interface*, void*),
                                         libdecor_frame*)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_capabilities, decorFrameSetCapabilities,
                                         (libdecor_frame*, int),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_visibility, decorFrameSetVisibility,
                                         (libdecor_frame*, bool),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_title, decorFrameSetTitle,
                                         (libdecor_frame*, const char*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_min_content_size, decorFrameSetMinContentSize,
                                         (libdecor_frame*, int, int),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_max_content_size, decorFrameSetMaxContentSize,
                                         (libdecor_frame*, int, int),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_map, decorFrameMap,
                                         (libdecor_frame*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_minimized, decorFrameSetMinimized,
                                         (libdecor_frame*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_set_maximized, decorFrameSetMaximized,
                                         (libdecor_frame*),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_unset_maximized, decorFrameUnsetMaximized,
                                         (libdecor_frame*),
                                         void)
  
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_is_floating, decorFrameIsFloating,
                                         (libdecor_frame*),
                                         bool)
  
    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_resize, decorFrameResize,
                                         (libdecor_frame*, wl_seat*, uint32_t,  int),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_move, decorFrameMove,
                                         (libdecor_frame*, wl_seat*, uint32_t),
                                         void)

    JUCE_GENERATE_FUNCTION_WITH_DEFAULT (libdecor_frame_unref, decorFrameUnref,
                                         (libdecor_frame*),
                                         void)
  
    template<typename T>
    Point<T> fixedToPoint (wl_fixed_t x, wl_fixed_t y)
    {
      if constexpr (std::is_integral_v<T>)
      {
          return Point<T>(fixedToInt (x), fixedToInt (y));
      }
      else {
        return Point<T>(fixedToDouble (x), fixedToDouble (y));
      }
    }
    
    using ProxyMarshalFlagsFn = wl_proxy*(*)(wl_proxy*, uint32_t, const struct wl_interface*, uint32_t, uint32_t, ...);
    ProxyMarshalFlagsFn proxyMarshalFlags;
    
    const wl_interface* displayInterface;
    const wl_interface* registryInterface;
    const wl_interface* callbackInterface;
    const wl_interface* compositorInterface;
    const wl_interface* shmPoolInterface;
    const wl_interface* shmInterface;
    const wl_interface* bufferInterface;
    const wl_interface* dataOfferInterface;
    const wl_interface* dataSourceInterface;
    const wl_interface* dataDeviceInterface;
    const wl_interface* dataDeviceManagerInterface;
    const wl_interface* shellInterface;
    const wl_interface* shellSurfaceInterface;
    const wl_interface* surfaceInterface;
    const wl_interface* seatInterface;
    const wl_interface* pointerInterface;
    const wl_interface* keyboardInterface;
    const wl_interface* touchInterface;
    const wl_interface* outputInterface;
    const wl_interface* regionInterface;
    const wl_interface* subcompositorInterface;
    const wl_interface* subsurfaceInterface;
    
    JUCE_DECLARE_SINGLETON_INLINE (WaylandSymbols, false)

private:
    WaylandSymbols() = default;

    ~WaylandSymbols()
    {
        clearSingletonInstance();
    }

    //==============================================================================
    DynamicLibrary waylandClientLib { "libwayland-client.so.0" };
    DynamicLibrary waylandCursorLib { "libwayland-cursor.so.0" };
    DynamicLibrary xkbLib        { "libxkbcommon.so.0" };
    DynamicLibrary libdecorLib   { "libdecor-0.so.0" };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaylandSymbols)
};

} // namespace juce

// Wayland calls these in inlined header functions, so we need to make them link to the dlopened functions
#define WL_MARSHAL_FLAG_DESTROY (1 << 0)
#define wl_display_interface \
  (*juce::WaylandSymbols::getInstance()->displayInterface)
#define wl_registry_interface \
  (*juce::WaylandSymbols::getInstance()->registryInterface)
#define wl_callback_interface \
  (*juce::WaylandSymbols::getInstance()->callbackInterface)
#define wl_compositor_interface \
  (*juce::WaylandSymbols::getInstance()->compositorInterface)
#define wl_shm_pool_interface \
  (*juce::WaylandSymbols::getInstance()->shmPoolInterface)
#define wl_shm_interface \
  (*juce::WaylandSymbols::getInstance()->shmInterface)
#define wl_buffer_interface \
  (*juce::WaylandSymbols::getInstance()->bufferInterface)
#define wl_data_offer_interface \
  (*juce::WaylandSymbols::getInstance()->dataOfferInterface)
#define wl_data_source_interface \
  (*juce::WaylandSymbols::getInstance()->dataSourceInterface)
#define wl_data_device_interface \
  (*juce::WaylandSymbols::getInstance()->dataDeviceInterface)
#define wl_data_device_manager_interface \
  (*juce::WaylandSymbols::getInstance()->dataDeviceManagerInterface)
#define wl_shell_interface \
  (*juce::WaylandSymbols::getInstance()->shellInterface)
#define wl_shell_surface_interface \
  (*juce::WaylandSymbols::getInstance()->shellSurfaceInterface)
#define wl_surface_interface \
  (*juce::WaylandSymbols::getInstance()->surfaceInterface)
#define wl_seat_interface \
  (*juce::WaylandSymbols::getInstance()->seatInerface)
#define wl_pointer_interface \
  (*juce::WaylandSymbols::getInstance()->pointerInterface)
#define wl_keyboard_interface \
  (*juce::WaylandSymbols::getInstance()->keyboardInterface)
#define wl_touch_interface \
  (*juce::WaylandSymbols::getInstance()->touchInterface)
#define wl_output_interface \
  (*juce::WaylandSymbols::getInstance()->outputInterface)
#define wl_region_interface \
  (*juce::WaylandSymbols::getInstance()->regionInterface)
#define wl_subcompositor_interface \
  (*juce::WaylandSymbols::getInstance()->subcompositorInterface)
#define wl_subsurface_interface \
  (*juce::WaylandSymbols::getInstance()->subsurfaceInterface)
#define wl_event_queue_destroys(q) \
  juce::WaylandSymbols::getInstance()->eventQueueDestroy (q)
#define wl_proxy_marshal_flags(p, o, i, v, f, ...) \
  juce::WaylandSymbols::getInstance()->proxyMarshalFlags (p, o, i, v, f, ##__VA_ARGS__)
#define wl_proxy_marshal_array_flags(p, o, i, v, f, a) \
  juce::WaylandSymbols::getInstance()->proxyMarshalArrayFlags (p, o, i, v, f, a)
#define wl_proxy_marshal_array(p, o, a) \
  juce::WaylandSymbols::getInstance()->proxyMarshalArray (p, o, a)
#define wl_proxy_create(f, i) \
  juce::WaylandSymbols::getInstance()->proxyCreate (f, i)
#define wl_proxy_create_wrapper(p) \
  juce::WaylandSymbols::getInstance()->proxyCreateWrapper (p)
#define wl_proxy_wrapper_destroy(pw) \
  juce::WaylandSymbols::getInstance()->proxyWrapperDestroy (pw)
#define wl_proxy_marshal_array_constructor(p, o, a, i) \
  juce::WaylandSymbols::getInstance()->proxyMarshalArrayConstructor (p, o, a, i)
#define wl_proxy_destroy(p) \
  juce::WaylandSymbols::getInstance()->proxyDestroy (p)
#define wl_proxy_add_listener(p, i, d) \
  juce::WaylandSymbols::getInstance()->proxyAddListener (p, i, d)
#define wl_proxy_get_listener(p) \
  juce::WaylandSymbols::getInstance()->proxyGetListener (p)
#define wl_proxy_add_dispatcher(p, df, dd, d) \
  juce::WaylandSymbols::getInstance()->proxyAddDispatcher (p, df, dd, d)
#define wl_proxy_set_user_data(p, ud) \
  juce::WaylandSymbols::getInstance()->proxySetUserData (p, ud)
#define wl_proxy_get_user_data(p) \
  juce::WaylandSymbols::getInstance()->proxyGetUserData (p)
#define wl_proxy_get_version(p) \
  juce::WaylandSymbols::getInstance()->proxyGetVersion (p)
#define wl_proxy_get_id(p) \
  juce::WaylandSymbols::getInstance()->proxyGetId (p)
#define wl_proxy_set_tag (p, t) \
  juce::WaylandSymbols::getInstance()->proxySetTag (p, t)
#define wl_proxy_get_tag(p) \
  juce::WaylandSymbols::getInstance()->proxyGetTag (p)
#define wl_proxy_get_class(p) \
  juce::WaylandSymbols::getInstance()->proxyGetClass (p)
#define wl_proxy_set_queue(p, q) \
  juce::WaylandSymbols::getInstance()->proxySetQueue (p, q)
#define wl_display_connect(n) \
  juce::WaylandSymbols::getInstance()->displayConnect (n)
#define wl_display_connect_to_fd(fd) \
  juce::WaylandSymbols::getInstance()->displayConnectToFd (fd)
#define wl_display_disconnect(d) \
  juce::WaylandSymbols::getInstance()->displayDisconnect (d)
#define wl_display_get_fd(d) \
  juce::WaylandSymbols::getInstance()->displayGetFd (d)
#define wl_display_dispatch(d) \
  juce::WaylandSymbols::getInstance()->displayDispatch (d)
#define wl_display_dispatch_queue(d, q) \
  juce::WaylandSymbols::getInstance()->displayDispatchQueue (d, q)
#define wl_display_dispatch_queue_pending(d, q) \
  juce::WaylandSymbols::getInstance()->displayDispatchQueuePending (d, q)
#define wl_display_dispatch_pending(d) \
  juce::WaylandSymbols::getInstance()->displayDispatchPending (d)
#define wl_display_get_error(d) \
  juce::WaylandSymbols::getInstance()->displayGetError (d)
#define wl_display_get_protocol_error(d, inf, id) \
  juce::WaylandSymbols::getInstance()->displayGetProtocolError (d, inf, id)
#define wl_display_flush(d) \
  juce::WaylandSymbols::getInstance()->displayFlush (d)
#define wl_display_roundtrip_queue(d, q) \
  juce::WaylandSymbols::getInstance()->displayRoundtripQueue (d, q)
#define wl_display_roundtrip(d) \
  juce::WaylandSymbols::getInstance()->displayRoundtrip (d)
#define wl_display_create_queue(d) \
  juce::WaylandSymbols::getInstance()->displayCreateQueue (d)
#define wl_display_prepare_read_queue(d, q) \
  juce::WaylandSymbols::getInstance()->displayPrepareReadQueue (d, q)
#define wl_display_prepare_read(d) \
  juce::WaylandSymbols::getInstance()->displayPrepareRead (d)
#define wl_display_cancel_read(d) \
  juce::WaylandSymbols::getInstance()->displayCancelRead (d)
#define wl_display_read_events(d) \
  juce::WaylandSymbols::getInstance()->displayReadEvents (d)
#define wl_log_set_handler_client(h) \
  juce::WaylandSymbols::getInstance()->logSetHandlerClient (h)

#define WAYLAND_CLIENT_CORE_H // Disable standard implementation.
#define WL_DISPLAY_INTERFACE
#define WL_REGISTRY_INTERFACE
#define WL_CALLBACK_INTERFACE
#define WL_COMPOSITOR_INTERFACE
#define WL_SHM_POOL_INTERFACE
#define WL_SHM_INTERFACE
#define WL_BUFFER_INTERFACE
#define WL_DATA_OFFER_INTERFACE
#define WL_DATA_SOURCE_INTERFACE
#define WL_DATA_DEVICE_INTERFACE
#define WL_DATA_DEVICE_MANAGER_INTERFACE
#define WL_SHELL_INTERFACE
#define WL_SHELL_SURFACE_INTERFACE
#define WL_SURFACE_INTERFACE
#define WL_SEAT_INTERFACE
#define WL_POINTER_INTERFACE
#define WL_KEYBOARD_INTERFACE
#define WL_TOUCH_INTERFACE
#define WL_OUTPUT_INTERFACE
#define WL_REGION_INTERFACE
#define WL_SUBCOMPOSITOR_INTERFACE
#define WL_SUBSURFACE_INTERFACE
