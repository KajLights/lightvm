/* PoC slots: 0x100000 and 0x110000. Existing production storage at 0x180000
 * and its preceding boot journal remain untouched. */
ASSERT(__flash_binary_end <= 0x10100000, "PoC firmware overlaps bytecode slots")
