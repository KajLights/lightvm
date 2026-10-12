#include "store.h"
#include "protocol.h"
#include "devices.h"
#include <string.h>
#include <limits.h>
#define DIRECTORY_BYTES 1024u
#define COMMIT_OFFSET 1024u
#define COMMIT_MAGIC 0x54494d43u
static uint32_t aligned(uint32_t size) { return (size + 255u) & ~255u; }
static bool entry_crc(int bank, const StoreEntry *entry) {
    uint8_t bytes[256];
    uint32_t crc = 0xffffffffu;
    for (uint32_t offset = 0; offset < entry->size;) {
        size_t n = entry->size - offset;
        if (n > sizeof(bytes)) n = sizeof(bytes);
        if (!platform_storage_read((uint32_t)bank * STORE_BANK_BYTES + entry->offset + offset, bytes, n)) return false;
        crc = crc32_update(crc, bytes, n); offset += (uint32_t)n;
    }
    return (crc ^ 0xffffffffu) == entry->crc;
}
static bool scan(int bank, Store *s) {
    uint8_t directory[DIRECTORY_BYTES], commit[256];
    uint32_t base = (uint32_t)bank * STORE_BANK_BYTES;
    if (!platform_storage_read(base, directory, sizeof(directory)) ||
        !platform_storage_read(base + COMMIT_OFFSET, commit, sizeof(commit))) return false;
    if (memcmp(directory, "LVS1", 4) || (read_u32(directory + 4) != 1 && read_u32(directory + 4) != 2) ||
        read_u32(directory + 1020) != crc32_bytes(directory, 1020) ||
        read_u32(commit) != COMMIT_MAGIC || read_u32(commit + 4) != read_u32(directory + 8) ||
        read_u32(commit + 8) != read_u32(directory + 1020) || read_u32(commit + 12) != crc32_bytes(commit, 12)) return false;
    memset(s, 0, sizeof(*s)); s->bank = bank; s->available = true;
    s->generation = read_u32(directory + 8); s->count = read_u32(directory + 12);
    s->config_revision = read_u32(directory + 16);
    s->network_revision = read_u32(directory + 4)==2 ? read_u32(directory + 20) : 0;
    if (!s->generation || s->count > STORE_ENTRY_LIMIT) return false;
    uint32_t expected = STORE_DATA_OFFSET;
    for (uint32_t i = 0; i < s->count; ++i) {
        const uint8_t *record = directory + 32 + i * 64;
        StoreEntry *entry = &s->entries[i];
        if (!memchr(record, 0, 33)) return false;
        memcpy(entry->name, record, 33);
        bool config = !strcmp(entry->name, STORE_CONFIG) || !strcmp(entry->name, STORE_NETWORK);
        if (!config && !object_name_valid(entry->name)) return false;
        for (uint32_t k = 0; k < i; ++k) if (!strcmp(s->entries[k].name, entry->name)) return false;
        entry->offset = read_u32(record + 36); entry->size = read_u32(record + 40); entry->crc = read_u32(record + 44);
        if (entry->offset != expected || !entry->size || entry->size > (config ? CONFIG_LIMIT : MODULE_LIMIT) ||
            aligned(entry->size) > STORE_BANK_BYTES - expected) return false;
        expected += aligned(entry->size);
        if (!entry_crc(bank, entry)) return false;
    }
    return true;
}
bool store_mount(Store *s) {
    Store a, b;
    memset(s, 0, sizeof(*s)); s->bank = -1; s->available = platform_storage_available();
    if (!s->available) return true;
    bool valid_a = scan(0, &a), valid_b = scan(1, &b);
    if (valid_a || valid_b) {
        *s = !valid_b || (valid_a && a.generation > b.generation) ? a : b;
        return true;
    }
    // A blank store is distinct from a corrupt or interrupted first snapshot.
    uint8_t bytes[256];
    for (uint32_t offset = 0; offset < STORAGE_BYTES; offset += sizeof(bytes)) {
        if (!platform_storage_read(offset, bytes, sizeof(bytes))) return false;
        for (size_t i = 0; i < sizeof(bytes); ++i) if (bytes[i] != 255) return false;
    }
    return true;
}
const StoreEntry *store_find(const Store *s, const char *name) {
    for (uint32_t i = 0; i < s->count; ++i) if (!strcmp(s->entries[i].name, name)) return &s->entries[i];
    return NULL;
}
bool store_read(const Store *s, const StoreEntry *entry, uint32_t offset, void *bytes, size_t size) {
    return s->bank >= 0 && entry && offset <= entry->size && size <= entry->size - offset &&
        platform_storage_read((uint32_t)s->bank * STORE_BANK_BYTES + entry->offset + offset, bytes, size);
}
uint32_t store_free_bytes(const Store *s) {
    uint32_t used = STORE_DATA_OFFSET;
    for (uint32_t i = 0; i < s->count; ++i) used += aligned(s->entries[i].size);
    return STORE_BANK_BYTES - used;
}
static bool mutate(Store *s, const char *name, const uint8_t *data, uint32_t size, bool remove) {
    if (!s->available || s->generation == UINT32_MAX) return false;
    bool config = !strcmp(name, STORE_CONFIG) || !strcmp(name, STORE_NETWORK);
    if (!config && !object_name_valid(name)) return false;
    if (!remove && (!size || size > (config ? CONFIG_LIMIT : MODULE_LIMIT))) return false;
    const StoreEntry *old = store_find(s, name);
    if (remove && !old) return false;
    Store next = *s;
    next.bank = s->bank == 0 ? 1 : 0; next.generation++; next.count = 0;
    if (!strcmp(name,STORE_NETWORK)) { if (next.network_revision==UINT32_MAX) return false; next.network_revision++; }
    if (!strcmp(name,STORE_CONFIG)) { if (next.config_revision == UINT32_MAX) return false; next.config_revision++; }
    uint32_t end = STORE_DATA_OFFSET;
    for (uint32_t i = 0; i < s->count; ++i) {
        if (!strcmp(s->entries[i].name, name)) continue;
        StoreEntry *e = &next.entries[next.count++]; *e = s->entries[i]; e->offset = end; end += aligned(e->size);
    }
    if (!remove) {
        if (next.count == STORE_ENTRY_LIMIT) return false;
        StoreEntry *e = &next.entries[next.count++];
        memset(e, 0, sizeof(*e)); memcpy(e->name, name, strlen(name) + 1);
        e->size = size; e->crc = crc32_bytes(data, size); e->offset = end; end += aligned(size);
    }
    unsigned procedures = 0;
    for (uint32_t i = 0; i < next.count; ++i) if (strcmp(next.entries[i].name, STORE_CONFIG) && strcmp(next.entries[i].name, STORE_NETWORK)) ++procedures;
    if (procedures > PROCEDURE_LIMIT || end > STORE_BANK_BYTES) return false;
    uint32_t base = (uint32_t)next.bank * STORE_BANK_BYTES;
    // Yield between native erase units; never erase the current committed bank.
    for (uint32_t offset = 0; offset < STORE_BANK_BYTES; offset += 4096)
        if (!platform_storage_erase(base + offset, 4096)) return false;
    uint8_t page[256];
    for (uint32_t i = 0; i < next.count; ++i) {
        const StoreEntry *e = &next.entries[i];
        const StoreEntry *source = store_find(s, e->name);
        bool replacement = !strcmp(e->name, name);
        for (uint32_t offset = 0; offset < e->size; offset += sizeof(page)) {
            size_t n = e->size - offset;
            if (n > sizeof(page)) n = sizeof(page);
            memset(page, 255, sizeof(page));
            if (replacement) memcpy(page, data + offset, n);
            else if (!store_read(s, source, offset, page, n)) return false;
            if (!platform_storage_write(base + e->offset + offset, page, sizeof(page))) return false;
        }
        if (!entry_crc(next.bank, e)) return false;
    }
    uint8_t directory[DIRECTORY_BYTES];
    memset(directory, 255, sizeof(directory)); memcpy(directory, "LVS1", 4);
    write_u32(directory + 4, 2); write_u32(directory + 8, next.generation);
    write_u32(directory + 20, next.network_revision);
    write_u32(directory + 12, next.count); write_u32(directory + 16, next.config_revision);
    for (uint32_t i = 0; i < next.count; ++i) {
        uint8_t *record = directory + 32 + i * 64;
        memcpy(record, next.entries[i].name, sizeof(next.entries[i].name));
        write_u32(record + 36, next.entries[i].offset); write_u32(record + 40, next.entries[i].size);
        write_u32(record + 44, next.entries[i].crc);
    }
    uint32_t crc = crc32_bytes(directory, 1020); write_u32(directory + 1020, crc);
    if (!platform_storage_write(base, directory, sizeof(directory)) || !platform_storage_sync()) return false;
    // Verify directory before the one-page commit, including read failures.
    for (uint32_t offset = 0; offset < sizeof(directory); offset += sizeof(page)) {
        if (!platform_storage_read(base + offset, page, sizeof(page)) || memcmp(page, directory + offset, sizeof(page))) return false;
    }
    memset(page, 255, sizeof(page)); write_u32(page, COMMIT_MAGIC); write_u32(page + 4, next.generation);
    write_u32(page + 8, crc); write_u32(page + 12, crc32_bytes(page, 12));
    if (!platform_storage_write(base + COMMIT_OFFSET, page, sizeof(page)) || !platform_storage_sync()) return false;
    Store verified;
    if (!scan(next.bank, &verified)) return false;
    *s = verified; return true;
}
bool store_put(Store *s, const char *name, const void *bytes, uint32_t size) { return mutate(s, name, bytes, size, false); }
bool store_delete(Store *s, const char *name) { return mutate(s, name, NULL, 0, true); }
