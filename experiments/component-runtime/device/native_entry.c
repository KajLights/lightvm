/* One import-free dispatch entry for the trusted ESP32-C3 native package.
 * The same audited implementation is used by the host experiment. */
#include "../native/component.c"

/* No jump table: build flags require -fno-jump-tables. No writable globals or
 * absolute data references are allowed in this flat PC-relative package. */
__attribute__((section(".text.native_entry"), used, visibility("default")))
int native_entry(uint32_t operation, NativeState *state, const NativeView *view,
                 uint32_t argument0, uint32_t argument1) {
    if (operation == 0u) return native_init(state, argument0, argument1, 0u);
    if (operation == 1u) return native_init_step(state, 32u);
    if (operation == 2u) return native_begin(state, argument0);
    if (operation == 3u) return native_step(state, view, 32u);
    if (operation == 4u) return native_cancel(state);
    /* Fixed query result checks the loaded entry and producer ABI. */
    if (operation == 5u) {
        const NativeInfo *info = native_info();
        if (info->abi_version != NATIVE_ABI || info->state_bytes != sizeof(NativeState)
            || info->recursion_allowed || info->heap_allowed) return NATIVE_STATE;
        return (int)(UINT32_C(0x4e430100) | info->artifact_revision);
    }
    return NATIVE_STATE;
}
