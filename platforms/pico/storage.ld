ASSERT(__flash_binary_end <= __lightvm_storage_start, "LightVM firmware overlaps reserved storage")
/* Both stacks have already been excluded from __HeapLimit by the SDK layout.
 * Resolve after all RAM code/data/BSS and alignment are placed. */
__lightvm_driver_reserve = __lightvm_output_slots * (__lightvm_pixel_capacity * 4 + 256);
__lightvm_heap_capacity = (__HeapLimit - __end__ - __lightvm_driver_reserve - __lightvm_native_headroom) & ~7;
__lightvm_vm_heap = __lightvm_heap_auto ? __lightvm_heap_capacity : __lightvm_heap_requested;
ASSERT(__HeapLimit >= __end__ + __lightvm_driver_reserve + __lightvm_native_headroom + 1024,
       "LightVM native reserves leave less than 1 KiB for the VM")
ASSERT(__lightvm_vm_heap <= __lightvm_heap_capacity,
       "LightVM requested VM heap exceeds RAM after native reserves")
