"""Explicit callback contracts for the configured Pico firmware.

Targets name source functions, including the GCC clones selected in a build.
Never use a zero-byte fallback for an unknown callback. See
``docs/design/pico-stack-accounting.md`` for ownership and recursion contracts.
"""
def enabled_target(name, features):
    """Only omit registrations compiled out by an explicit firmware option.

    Do not infer absence from a missing symbol: that must still fail the gate.
    """
    if features is None:return True
    if (name.startswith(('src/home_assistant.c:', 'src/ha_keys.c:', 'platforms/pico/mqtt_port.c:')) or
        name in ('ha_tick','mqtt_tick','error_stream','resolved')) and not features['LIGHTVM_MQTT']:return False
    if (name.startswith(('src/http.c:', 'src/discovery.c:')) or name in ('http_tick','failed','discovery_received') or
        name.startswith('platforms/pico/network_port.c:') and name.split(':')[-1] in
        ('output','close_client','receive','accept_client','failed','discovery_send','discovery_received')) and not features['LIGHTVM_HTTP']:return False
    if (name.startswith('platforms/pico/network_port.c:') or name=='cyw43_gpio_irq_handler' or
        name.endswith('/hardware_gpio/gpio.c:gpio_default_irq_handler')) and not features['LIGHTVM_WIFI']:return False
    return True

PIXELS = tuple('platform_'+kind+'_'+op for kind in ('pixels','apa102')
               for op in ('open','close','show','submit','poll','cancel'))
SEND = ('src/session.c:send_message', 'platforms/pico/main.c:output_message',
        'src/http.c:capture', 'src/home_assistant.c:response')
WRITE = ('platforms/pico/main.c:output_bytes', 'src/http.c:capture_bytes')
RANGES = ('src/transition.c:compose_pixels',
          'src/composition.c:compose_pixels')
READ = ('vm_read', 'src/vm.c:stored_read')
NETWORK = tuple('platforms/pico/network_port.c:'+name for name in
                ('configure','scan','scan_poll','connect','link','disconnect','random_value'))
STREAM = tuple('platforms/pico/mqtt_port.c:'+name+'_stream' for name in
               ('open','close','poll','read','write'))
CALLBACKS = {
    'runtime_checkpoint': ('src/app.c:service',),
    'src/vm.c:tick_prepare': PIXELS,
    'platforms/pico/main.c:output_bytes': ('stdio_usb_out_chars',),
    'src/app.c:service': ('platforms/pico/main.c:service_io',),
    'render_job_execute': ('animation_execute','src/render_executor.c:range_run','src/portable_map.c:map_pixels','src/portable_draw.c:draw_task','platforms/pico/render_executor.c:stack_probe'),
    'src/render_executor.c:range_run': RANGES,
    'render_ranges': RANGES,
    'render_execute': ('render_job_execute',),
    'src/render_executor.c:render_execute': ('render_job_execute',),
    'src/render_executor.c:render_platform_submit': ('render_job_execute',),
    'send_status': SEND, 'write_frame': WRITE,
    'src/protocol.c:finish_frame': ('src/session.c:receive_message',)+SEND,
    'src/session.c:send_message': SEND[1:],
    'src/app.c:vm_send': SEND,
    'src/control.c:bytes': ('src/app.c:vm_send',),
    'control_manage': ('src/app.c:vm_send',),
    'input_binding_describe': ('src/app.c:binding_text','src/control.c:input_text'),
    'inputs_describe': ('src/control.c:input_text',),
    'src/outputs.c:parse': ('src/outputs.c:validate_pixels','src/outputs.c:validate_apa102'),
    'http_tick': ('platforms/pico/network_port.c:output','platforms/pico/network_port.c:close_client'),
    'mqtt_tick': STREAM+('src/home_assistant.c:message',), 'mqtt_close': STREAM,
    'network_apply': NETWORK, 'network_tick': NETWORK,
    'src/network.c:configure_radio': NETWORK, 'src/network.c:backoff': NETWORK, 'src/network.c:next': NETWORK,
    'src/discovery.c:query': ('platforms/pico/network_port.c:discovery_send',),
    'src/discovery.c:response': ('platforms/pico/network_port.c:discovery_send',),
}
for name in ('bytes','flush','describe','empty_config','config_apply','composition_output_command',
             'composition_state_for','error_reply','indicator_command','indicator_tick','key_feedback_command',
             'light_command_execute','manage_dispatch','manage_network','network_get','number','preset_manage',
             'quoted','read_object','real_number','transition_state','wide','binding_text'):
    CALLBACKS['src/app.c:'+name]=SEND+PIXELS
CALLBACKS['app_preparation_commit']=PIXELS
CALLBACKS['src/preparation.c:record_reply']=SEND
for name in ('composition_destroy','composition_frame','composition_offscreen','transition_destroy','transition_frame',
             'pixels_destroy','pixels_init','pixels_show','pixels_show_frame','pixels_submit','vm_command','vm_destroy',
             'vm_output_cancel','vm_tick_collect'):
    CALLBACKS[name]=PIXELS+(() if name not in ('vm_command',) else ('src/app.c:vm_send',))
for name in ('src/composition.c:complete_output','src/transition.c:complete','src/vm.c:complete_output',
             'src/vm.c:show_frame','src/vm.c:stop_animation'):
    CALLBACKS[name]=PIXELS
for name in ('pbc_call','pbc_expression','pbc_function','pbc_image_read','pbc_initial','pbc_invoke','pbc_open_step',
             'pbc_prepare_step','pbc_read_instruction','pbc_render_reusable','pbc_resume','pbc_setting','pbc_spec','pbc_value',
             'src/portable.c:evaluate','src/portable.c:instruction','src/portable.c:resource_write','src/portable_draw.c:transform_pixels'):
    CALLBACKS[name]=READ
for caller, targets in {
    'store_delete': ('entry','remove_entry'), 'store_destroy': ('destroy',), 'store_find': ('entry',),
    'store_info': ('info',), 'store_list': ('entry',), 'store_mount': ('mount',), 'store_mount_with': ('mount','destroy'),
    'store_pin': ('retain',), 'store_publish': ('publish',), 'store_put': ('write_step','write_cancel'),
    'store_put_config': ('write_step','write_cancel'), 'store_read': ('read_bytes',), 'store_unpin': (),
    'store_upload_begin': ('begin',), 'store_upload_cancel': ('cancel',), 'store_upload_chunk': ('chunk',),
    'store_upload_finish': ('finish',), 'store_write_cancel': ('write_cancel',), 'store_write_step': ('write_step',),
}.items(): CALLBACKS[caller]=tuple('src/store_extent.c:'+n for n in targets)
for name in ('chunk','commit_step','extent_crc','finish','mount','read_bytes','scan','write_step'):
    CALLBACKS['src/store_extent.c:'+name]=tuple('src/store_extent.c:block_'+n for n in ('read','write','erase','sync'))

# Configured SDK callback registrations. Uninstalled hooks have an explicit
# empty set; new source/SDK callsites remain errors until assigned here.
SDK_CALLBACKS = {
    'async_context_base_execute_once': ('cyw43_do_poll','cyw43_sleep_timeout_reached','update_next_timeout','lwip_timeout_reached'),
    'async_context_poll_execute_sync': ('cyw43_irq_init','cyw43_irq_deinit'),
    'cyw43_cb_process_async_event': ('platforms/pico/network_port.c:result',),
    'cyw43_cb_process_ethernet': ('ethernet_input',),
    'cyw43_do_poll': ('cyw43_poll_func',),
    'cyw43_gpio_irq_handler': ('async_context_poll_requires_update',),
    'cyw43_sleep_timeout_reached': (),
    'cyw43_schedule_internal_poll_dispatch': (),
    'cyw43_thread_enter': ('noop',),
    'cyw43_thread_exit': ('noop',),
    'cyw43_arch_poll': ('async_context_poll_poll',),
    'cyw43_arch_wait_for_work_until': ('async_context_poll_wait_for_work_until',),
    'cyw43_delay_ms': ('async_context_poll_wait_until',),
    'cyw43_delay_us': (),
    'cyw43_driver_init': ('async_context_poll_execute_sync','async_context_base_add_when_pending_worker','async_context_base_add_at_time_worker'),
    'cyw43_driver_deinit': ('async_context_poll_execute_sync','async_context_base_remove_when_pending_worker','async_context_base_remove_at_time_worker'),
    'cyw43_arch_init': ('async_context_poll_init_with_defaults',),
    'cyw43_arch_deinit': ('noop',),
    'cyw43_await_background_or_timeout_us': ('async_context_poll_wait_for_work_until',),
    'update_next_timeout': ('async_context_base_add_at_time_worker',),
    'lwip_nosys_init': ('async_context_base_add_when_pending_worker',),
    'lwip_nosys_deinit': ('async_context_base_remove_when_pending_worker','async_context_base_remove_at_time_worker'),
    'netif_add': ('cyw43_netif_init','ethernet_input'),
    'ethernet_output': ('cyw43_netif_output',),
    'ip4_output_if_opt_src': ('etharp_output',),
    'ip4_output_if_opt': ('etharp_output',), 'ip4_output_if': ('etharp_output',),
    'ip4_output': ('etharp_output',), 'ip4_output_if_src': ('etharp_output',), 'ip4_output_if_opt_src': ('etharp_output',),
    'ip4_output_if_src': ('etharp_output',), 'ip4_output_if_opt': ('etharp_output',),
    'ip4_frag': ('etharp_output',),
    'tcp_input': ('platforms/pico/network_port.c:receive','platforms/pico/network_port.c:accept_client','platforms/pico/mqtt_port.c:received','platforms/pico/mqtt_port.c:connected','platforms/pico/network_port.c:failed','platforms/pico/mqtt_port.c:error_stream','tcp_recv_null'),
    'tcp_process_refused_data': ('platforms/pico/network_port.c:receive','platforms/pico/mqtt_port.c:received','tcp_recv_null'),
    'tcp_abandon': ('failed','error_stream'), 'tcp_slowtmr': ('failed','error_stream'),
    'udp_input': ('discovery_received','dhcp_recv','dns_recv','platforms/pico/dhcp_port.c:received'),
    'dns_call_found': ('resolved',), 'dns_recv': ('resolved',),
    'sys_check_timeouts': ('lwip_cyclic_timer','tcpip_tcp_timer'),
    'lwip_cyclic_timer': ('tcp_tmr','ip_reass_tmr','etharp_tmr','dhcp_coarse_tmr','dhcp_fine_tmr','dns_tmr','igmp_tmr','acd_tmr'),
    '_vsnprintf': ('_out_buffer','_out_null','_out_char'), '_out_rev': ('_out_buffer','_out_null','_out_char'),
    '_etoa': ('_out_buffer','_out_null','_out_char'), '_out_fct': ('_putchar',),
    'stdio_out_chars_crlf': ('stdio_usb_out_chars',), 'stdio_out_chars_no_crlf': ('stdio_usb_out_chars',),
    'stdio_flush': ('stdio_usb_out_flush',), 'stdio_get_until': ('stdio_usb_in_chars',),
    'stdio_getchar_timeout_us': ('stdio_usb_in_chars',), '__wrap_getchar': ('stdio_usb_in_chars',),
    'stdio_put_string': ('stdio_usb_out_chars',), 'stdio_puts_raw': ('stdio_usb_out_chars',), '__wrap_puts': ('stdio_usb_out_chars',),
    'stdio_set_chars_available_callback': ('stdio_usb_set_chars_available_callback',),
    'stdio_usb_call_chars_available_callback': (), 'low_priority_worker_irq': (),
    'stdio_deinit_all': ('stdio_usb_deinit',),
    'flash_safe_execute': ('default_enter_safe_zone_timeout_ms','default_exit_safe_zone_timeout_ms','platforms/pico/platform.c:flash_operation'),
    'flash_safe_execute_core_init': ('default_core_init_deinit',), 'flash_safe_execute_core_deinit': ('default_core_init_deinit',),
    'rom_func_lookup': ('@rom_lookup',), 'rom_data_lookup': ('@rom_lookup',), 'rom_funcs_lookup': ('@rom_lookup',),
    'flash_range_erase': ('@rom_flash',), 'flash_range_program': ('@rom_flash',),
    'flash_enable_xip_via_boot2': ('@boot2',), 'flash_start_xip': ('@rom_flash',),
    'flash_flush_cache': ('@rom_flash',), 'flash_do_cmd_cs': ('@rom_flash',),
    'alarm_pool_irq_handler': ('sleep_until_callback','timer_task'),
    'hardware_alarm_irq_handler': ('alarm_pool_irq_handler',),
    'gpio_default_irq_handler': ('cyw43_gpio_irq_handler',),
    'rtc_irq_handler': (), 'clocks_irq_handler': (),
    'pbuf_free': (),
    'ph_merge_two_pass': ('alarm_pool_timer_compare',), 'ph_merge_nodes': ('alarm_pool_timer_compare',),
    'acd_arp_reply': ('dhcp_conflict_callback',), 'acd_netif_ip_addr_changed': ('dhcp_conflict_callback',), 'acd_tmr': ('dhcp_conflict_callback',),
    'netif_do_set_ipaddr': (), 'netif_set_addr': (), 'netif_set_down': (), 'netif_set_ipaddr': (), 'netif_set_link_down': (),
    'netif_set_link_up': (), 'netif_set_up': (), 'netif_remove': (),
    'igmp_joingroup': (), 'igmp_joingroup_netif': (), 'igmp_leavegroup_netif': (), 'igmp_start': (), 'igmp_stop': (),
    'dcd_event_handler': (),
    'tud_task_ext': ('cdcd_open','cdcd_control_xfer_cb','cdcd_xfer_cb','cdcd_reset','tud_vendor_control_xfer_cb'),
    'tud_rhport_init': ('cdcd_init',), 'tud_deinit': ('cdcd_deinit',), 'usbd_control_xfer_cb': ('cdcd_control_xfer_cb',),
    'i2c_read_blocking_internal': ('check_single_timeout_us','check_per_iteration_timeout_us'),
    'i2c_write_blocking_internal': ('check_single_timeout_us','check_per_iteration_timeout_us'),
    'rom_reset_usb_boot_extra': ('@rom_flash',),
    '__wrap_vprintf': ('_vsnprintf',),
    'weak_raw_printf': ('__wrap_printf',), 'weak_raw_vprintf': ('__wrap_vprintf',),
    '_mbrtowc_r': ('__ascii_mbtowc',),
    '__ssvfscanf_r': ('__ascii_mbtowc',),
    'core1_wrapper': ('platforms/pico/render_executor.c:worker_main',),
}

for name in ('__wrap___aeabi_i2f','__aeabi_i2f_main','__wrap___aeabi_f2d','__wrap___aeabi_f2uiz','__wrap___aeabi_fadd','__wrap___aeabi_fmul','__wrap___aeabi_fsub','__wrap___aeabi_l2f','__wrap_sinf','fdiv_shim_call','fix2float','float2int'):
    SDK_CALLBACKS[name]=('@rom_float',)
for name in ('__wrap___aeabi_memcpy','__wrap___clz','__wrap___ctzsi2','__wrap_memset'):
    SDK_CALLBACKS[name]=('@rom_memory',)
SDK_CALLBACKS.update({
    'configuration_reset': ('cdcd_reset',),
    'flash_devinfo_ptr': ('@rom_lookup',),
    'flash_rp2350_restore_qmi_cs1': (), # no CS1 setup hook installed
    'rom_reboot': ('@rom_flash',),
    'rom_get_boot_random': ('@rom_info',),
    'runtime_init_per_core_bootrom_reset': ('@rom_info',),
    'generic_save_state': ('@dcp_body',), # resumes after the PCMP/branch, not at its entry
})

# Closed, source-audited assembly routines whose unnamed shared entries cannot
# be modelled as ordinary C calls. Checked SDK fingerprints protect this audit.
# DCP: saved LR 4 + state/scratch 32 + largest arithmetic body 8 = 44; allow 64.
# VFP sin/cos: one 20-byte range-reduction frame; frr_core is a leaf (0),
# repeated reductions reuse that frame. Allow 32 for the complete routine.
ASSEMBLY_PATHS = {
    'dcp': {**{'__wrap___aeabi_d'+op:(64,()) for op in ('add','sub','rsub','mul','div')},
            'generic_save_state':(40,()), 'generic_restore_state':(0,())},
    'vfp_trig': {'__wrap_sinf':(32,()),'__wrap_cosf':(32,()),'frr_core':(0,())},
}
