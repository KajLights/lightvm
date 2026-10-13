#include "boot_sequence.h"
#include "platform.h"
#include "protocol.h"
#include <string.h>

#define SECTOR_BYTES 4096u
#define PAGE_BYTES 256u
#define PAGES_PER_SECTOR (SECTOR_BYTES / PAGE_BYTES)
#define RECORD_VERSION 1u
#define CRC_OFFSET (PAGE_BYTES - 4u)
_Static_assert(BOOT_SEQUENCE_STORAGE_BYTES == 2u * SECTOR_BYTES, "boot journal needs two sectors");

static bool erased(const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) if (bytes[i] != 255) return false;
    return true;
}
static uint64_t record_sequence(const uint8_t *page) {
    return (uint64_t)read_u32(page + 8) | ((uint64_t)read_u32(page + 12) << 32);
}
static bool valid(const uint8_t *page) {
    return !memcmp(page, "LVBQ", 4) && read_u32(page + 4) == RECORD_VERSION &&
        record_sequence(page) && read_u32(page + 16) <= BOOT_SEQUENCE_THRESHOLD &&
        read_u32(page + CRC_OFFSET) == crc32_bytes(page, CRC_OFFSET);
}
static bool failed(BootSequence *state, const char *message) {
    state->error = message;
    return false;
}
static bool append(BootSequence *state, uint32_t count) {
    if (state->sequence == UINT64_MAX) return failed(state, "journal sequence exhausted");
    uint32_t sector = state->sector, next_page = state->next_page;
    uint8_t page[PAGE_BYTES], check[PAGE_BYTES];
    if (next_page == PAGES_PER_SECTOR) {
        sector ^= 1u;
        if (!platform_boot_storage_erase(sector * SECTOR_BYTES, SECTOR_BYTES) ||
            !platform_boot_storage_sync()) return failed(state, "journal erase failed");
        // An interrupted erase can leave old records: verify the entire sector.
        for (uint32_t i = 0; i < PAGES_PER_SECTOR; ++i) {
            if (!platform_boot_storage_read(sector * SECTOR_BYTES + i * PAGE_BYTES, check, sizeof(check)) ||
                !erased(check, sizeof(check))) return failed(state, "journal erase verification failed");
        }
        next_page = 0;
    }
    uint32_t offset = sector * SECTOR_BYTES + next_page * PAGE_BYTES;
    uint64_t sequence = state->sequence + 1;
    memset(page, 255, sizeof(page));
    memcpy(page, "LVBQ", 4); write_u32(page + 4, RECORD_VERSION);
    write_u32(page + 8, (uint32_t)sequence); write_u32(page + 12, (uint32_t)(sequence >> 32));
    write_u32(page + 16, count); write_u32(page + CRC_OFFSET, crc32_bytes(page, CRC_OFFSET));
    if (!platform_boot_storage_write(offset, page, sizeof(page))) return failed(state, "journal write failed");
    if (!platform_boot_storage_read(offset, check, sizeof(check)) || memcmp(page, check, sizeof(page)))
        return failed(state, "journal write verification failed");
    if (!platform_boot_storage_sync()) return failed(state, "journal sync failed");
    state->sector = sector; state->next_page = next_page + 1;
    state->sequence = sequence; state->count = count;
    return true;
}
bool boot_sequence_init(BootSequence *state) {
    memset(state, 0, sizeof(*state));
    state->started_us = platform_time_us(); state->error = "";
    if (!platform_boot_storage_available()) return failed(state, "storage unavailable");
    uint32_t used[2] = {0};
    uint8_t page[PAGE_BYTES];
    for (uint32_t sector = 0; sector < 2; ++sector) {
        for (uint32_t i = 0; i < PAGES_PER_SECTOR; ++i) {
            if (!platform_boot_storage_read(sector * SECTOR_BYTES + i * PAGE_BYTES, page, sizeof(page)))
                return failed(state, "journal read failed");
            // Never reuse a partially written page, even if its record is invalid.
            if (!erased(page, sizeof(page))) used[sector] = i + 1;
            if (valid(page) && record_sequence(page) > state->sequence) {
                state->sequence = record_sequence(page); state->count = read_u32(page + 16);
                state->sector = sector;
            }
        }
    }
    if (!state->sequence && used[0] == PAGES_PER_SECTOR && used[1] < PAGES_PER_SECTOR) state->sector = 1;
    state->next_page = used[state->sector];
    uint32_t count = state->count < BOOT_SEQUENCE_THRESHOLD ? state->count + 1 : BOOT_SEQUENCE_THRESHOLD;
    if (!append(state, count)) return false;
    state->triggered = count == BOOT_SEQUENCE_THRESHOLD;
    state->reset_pending = true;
    return true;
}
void boot_sequence_tick(BootSequence *state, uint64_t now) {
    if (!state->reset_pending || now < state->started_us || now - state->started_us < BOOT_SEQUENCE_WINDOW_US) return;
    state->reset_pending = false;
    (void)append(state, 0);
}
