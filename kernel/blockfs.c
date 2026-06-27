#include <block.h>
#include <blockfs.h>
#include <log.h>
#include <stdint.h>
#include <vfs.h>

#define BLOCKFS_MAX_MOUNTS 4
#define BLOCKFS_INFO_MAX 8192
#define BLOCKFS_MAX_EXFAT_ROOT_ENTRIES 32
#define BLOCKFS_MAX_ROOT_ENTRIES BLOCKFS_MAX_EXFAT_ROOT_ENTRIES
#define EXFAT_ENTRY_SIZE 32
#define EXFAT_ALLOC_PLAN_CLUSTER_COUNT 3
#define EXFAT_TEST_FILE_DEFAULT_CLUSTERS EXFAT_ALLOC_PLAN_CLUSTER_COUNT
#define EXFAT_CREATE_PLAN_NAME "NEW.TXT"
#define EXFAT_CREATE_PLAN_NAME_CHARS 7
#define EXFAT_CREATE_PLAN_MAX_NAME_CHARS 15
#define EXFAT_CREATE_PLAN_REQUIRED_ENTRIES 3
#define EXFAT_CREATE_PLAN_ENTRY_BYTES (EXFAT_CREATE_PLAN_REQUIRED_ENTRIES * EXFAT_ENTRY_SIZE)
#define EXFAT_TEST_TX_MAX_SECTORS 4

struct exfat_boot_info {
    int present;
    uint64_t partition_offset;
    uint64_t volume_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint32_t volume_serial;
    uint16_t revision;
    uint16_t flags;
    uint8_t bytes_per_sector_shift;
    uint8_t sectors_per_cluster_shift;
    uint8_t number_of_fats;
    uint8_t drive_select;
    uint8_t percent_in_use;
    int allocation_bitmap_present;
    uint8_t allocation_bitmap_flags;
    uint32_t allocation_bitmap_cluster;
    uint64_t allocation_bitmap_size;
    uint64_t allocation_bitmap_scanned_clusters;
    uint64_t allocation_bitmap_used_clusters;
    uint64_t allocation_bitmap_free_clusters;
    uint32_t allocation_bitmap_first_free_cluster;
    int allocation_bitmap_complete;
    uint64_t allocation_plan_requested_clusters;
    uint64_t allocation_plan_found_clusters;
    uint32_t allocation_plan_clusters[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    int allocation_plan_ready;
    uint64_t create_plan_required_entries;
    uint64_t create_plan_found_entries;
    uint32_t create_plan_directory_cluster;
    uint64_t create_plan_directory_cluster_index;
    uint64_t create_plan_sector_index;
    uint64_t create_plan_first_slot;
    uint64_t create_plan_entry_lba;
    int create_plan_ready;
    uint64_t create_plan_file_size;
    uint32_t create_plan_first_cluster;
    uint16_t create_plan_name_hash;
    uint16_t create_plan_entry_set_checksum;
    uint8_t create_plan_entries[EXFAT_CREATE_PLAN_ENTRY_BYTES];
    int create_plan_entries_ready;
    int transaction_plan_ready;
    uint64_t transaction_plan_write_count;
    uint64_t transaction_bitmap_lba;
    uint64_t transaction_bitmap_byte_offset;
    uint8_t transaction_bitmap_old_value;
    uint8_t transaction_bitmap_new_value;
    uint8_t transaction_bitmap_mask;
    uint64_t transaction_fat_lba;
    uint64_t transaction_fat_offsets[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint32_t transaction_fat_old_values[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint32_t transaction_fat_new_values[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint64_t transaction_directory_lba;
    uint64_t transaction_directory_byte_offset;
    uint64_t transaction_directory_byte_count;
    int patched_transaction_ready;
    uint8_t patched_bitmap_value;
    uint32_t patched_fat_values[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint16_t patched_directory_checksum;
    uint32_t patched_directory_first_cluster;
    uint64_t patched_directory_file_size;
    uint8_t patched_directory_name_length;
    int patched_directory_name_matches;
    int commit_supported;
    int commit_attempted;
    int commit_ready;
    uint64_t commit_write_count;
    int commit_verified;
    int upcase_table_present;
    uint32_t upcase_table_checksum;
    uint32_t upcase_table_cluster;
    uint64_t upcase_table_size;
};

struct exfat_root_entry {
    char parent[VFS_MOUNT_PATH_MAX];
    char name[VFS_DIRENT_NAME_MAX];
    uint64_t size;
    uint64_t capacity;
    uint64_t directory_lba;
    uint64_t directory_byte_offset;
    uint32_t first_cluster;
    uint16_t attributes;
    uint8_t stream_flags;
    int is_dir;
    int writable;
};

struct fat32_boot_info {
    int present;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint32_t total_sectors;
    uint32_t fat_size_sectors;
    uint32_t root_dir_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint32_t hidden_sectors;
    uint32_t volume_id;
    char volume_label[12];
};

struct fat32_lfn_state {
    char name[VFS_DIRENT_NAME_MAX];
    uint8_t checksum;
    uint8_t total_entries;
    uint8_t seen_entries;
    int present;
    int valid;
};

struct blockfs_mount {
    int in_use;
    uint64_t block_index;
    struct block_device_info block;
    char source[VFS_MOUNT_SOURCE_MAX];
    struct exfat_boot_info exfat;
    struct fat32_boot_info fat32;
    struct exfat_root_entry root_entries[BLOCKFS_MAX_ROOT_ENTRIES];
    uint64_t root_entry_count;
};

static struct blockfs_mount mounts[BLOCKFS_MAX_MOUNTS];

static void commit_exfat_create_transaction_for_test(struct blockfs_mount *mount);
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
static int refresh_exfat_test_file_size_for_test(struct blockfs_mount *mount,
                                                 struct exfat_root_entry *entry,
                                                 uint64_t size);
static int rename_exfat_test_file_for_test(struct blockfs_mount *mount, const char *path);
static int create_exfat_test_file_with_clusters_for_test(struct blockfs_mount *mount,
                                                         const char *path,
                                                         uint64_t requested_clusters);
static int unlink_exfat_test_file_for_test(struct blockfs_mount *mount, const char *path);
#endif
static struct exfat_root_entry *find_exfat_root_entry(struct blockfs_mount *mount, const char *path);

static int chars_equal(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void copy_limited(char *dst, uint64_t dst_len, const char *src) {
    uint64_t i = 0;

    if (dst_len == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
static uint64_t cstr_length(const char *s) {
    uint64_t len = 0;

    if (s == 0) {
        return 0;
    }
    while (s[len] != '\0') {
        len++;
    }
    return len;
}
#endif

static void copy_bytes(void *dst, const void *src, uint64_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;

    for (uint64_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void zero_bytes(void *dst, uint64_t size) {
    uint8_t *d = dst;

    for (uint64_t i = 0; i < size; i++) {
        d[i] = 0;
    }
}

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t read_le64(const uint8_t *data) {
    return (uint64_t)read_le32(data) | ((uint64_t)read_le32(data + 4) << 32);
}

static void write_le16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *data, uint64_t value) {
    write_le32(data, (uint32_t)value);
    write_le32(data + 4, (uint32_t)(value >> 32));
}

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
typedef int (*exfat_sector_mutator_fn)(uint8_t *sector, void *context);

struct exfat_bit_patch {
    uint64_t offset;
    uint8_t mask;
    int set;
};

struct exfat_u32_patch {
    uint64_t offset;
    uint32_t value;
};

struct exfat_bytes_patch {
    uint64_t offset;
    const uint8_t *bytes;
    uint64_t size;
};

struct exfat_delete_entries_patch {
    uint64_t offset;
    uint64_t entry_count;
};

struct exfat_sector_transaction_entry {
    uint64_t lba;
    uint8_t sector[512];
};

struct exfat_sector_transaction {
    struct blockfs_mount *mount;
    uint64_t count;
    struct exfat_sector_transaction_entry entries[EXFAT_TEST_TX_MAX_SECTORS];
};

static int raw_bytes_equal(const uint8_t *a, const uint8_t *b, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int write_exfat_sector_checked_for_test(struct blockfs_mount *mount,
                                               uint64_t lba,
                                               const uint8_t *sector) {
    uint8_t verify[512];

    if (mount == 0 || sector == 0 || mount->block.block_size != sizeof(verify)) {
        return -1;
    }
    if (block_write(mount->block_index, lba, 1, sector) != 0 ||
        block_read(mount->block_index, lba, 1, verify) != 0 ||
        !raw_bytes_equal(sector, verify, sizeof(verify))) {
        return -1;
    }
    return 0;
}

static int apply_exfat_bit_patch(uint8_t *sector, void *context) {
    struct exfat_bit_patch *patch = context;

    if (sector == 0 || patch == 0 || patch->offset >= 512) {
        return -1;
    }
    if (patch->set) {
        sector[patch->offset] |= patch->mask;
    } else {
        sector[patch->offset] &= (uint8_t)~patch->mask;
    }
    return 0;
}

static int apply_exfat_u32_patch(uint8_t *sector, void *context) {
    struct exfat_u32_patch *patch = context;

    if (sector == 0 || patch == 0 || patch->offset + 4 > 512) {
        return -1;
    }
    write_le32(sector + patch->offset, patch->value);
    return 0;
}

static int apply_exfat_bytes_patch(uint8_t *sector, void *context) {
    struct exfat_bytes_patch *patch = context;

    if (sector == 0 || patch == 0 || patch->bytes == 0 ||
        patch->offset + patch->size > 512) {
        return -1;
    }
    copy_bytes(sector + patch->offset, patch->bytes, patch->size);
    return 0;
}

static int apply_exfat_delete_entries_patch(uint8_t *sector, void *context) {
    struct exfat_delete_entries_patch *patch = context;

    if (sector == 0 || patch == 0 ||
        patch->offset + patch->entry_count * EXFAT_ENTRY_SIZE > 512) {
        return -1;
    }
    for (uint64_t i = 0; i < patch->entry_count; i++) {
        uint64_t offset = patch->offset + i * EXFAT_ENTRY_SIZE;
        if ((sector[offset] & 0x80) == 0) {
            return -1;
        }
        sector[offset] &= 0x7f;
    }
    return 0;
}

static void exfat_sector_transaction_begin_for_test(struct exfat_sector_transaction *tx,
                                                    struct blockfs_mount *mount) {
    if (tx == 0) {
        return;
    }
    tx->mount = mount;
    tx->count = 0;
}

static uint8_t *exfat_sector_transaction_get_for_test(struct exfat_sector_transaction *tx,
                                                      uint64_t lba) {
    struct exfat_sector_transaction_entry *entry;

    if (tx == 0 || tx->mount == 0 || tx->mount->block.block_size != 512) {
        return 0;
    }
    for (uint64_t i = 0; i < tx->count; i++) {
        if (tx->entries[i].lba == lba) {
            return tx->entries[i].sector;
        }
    }
    if (tx->count >= EXFAT_TEST_TX_MAX_SECTORS) {
        return 0;
    }

    entry = &tx->entries[tx->count];
    entry->lba = lba;
    if (block_read(tx->mount->block_index, lba, 1, entry->sector) != 0) {
        return 0;
    }
    tx->count++;
    return entry->sector;
}

static int exfat_sector_transaction_mutate_for_test(struct exfat_sector_transaction *tx,
                                                    uint64_t lba,
                                                    exfat_sector_mutator_fn mutator,
                                                    void *context) {
    uint8_t *sector = exfat_sector_transaction_get_for_test(tx, lba);

    if (sector == 0 || mutator == 0 || mutator(sector, context) != 0) {
        return -1;
    }
    return 0;
}

static int exfat_sector_transaction_verify_for_test(struct exfat_sector_transaction *tx) {
    uint8_t verify[512];

    if (tx == 0 || tx->mount == 0 || tx->mount->block.block_size != sizeof(verify)) {
        return -1;
    }
    for (uint64_t i = 0; i < tx->count; i++) {
        if (block_read(tx->mount->block_index, tx->entries[i].lba, 1, verify) != 0 ||
            !raw_bytes_equal(tx->entries[i].sector, verify, sizeof(verify))) {
            return -1;
        }
    }
    return 0;
}

static int exfat_sector_transaction_commit_for_test(struct exfat_sector_transaction *tx) {
    if (tx == 0 || tx->mount == 0) {
        return -1;
    }
    for (uint64_t i = 0; i < tx->count; i++) {
        if (write_exfat_sector_checked_for_test(tx->mount,
                                                tx->entries[i].lba,
                                                tx->entries[i].sector) != 0) {
            return -1;
        }
    }
    return exfat_sector_transaction_verify_for_test(tx);
}
#endif

static int bytes_equal(const uint8_t *data, const char *text, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        if (data[i] != (uint8_t)text[i]) {
            return 0;
        }
    }
    return 1;
}

static void copy_padded_text(char *dst, uint64_t dst_len, const uint8_t *src, uint64_t src_len) {
    uint64_t out = 0;

    if (dst_len == 0) {
        return;
    }
    while (src_len > 0 && src[src_len - 1] == ' ') {
        src_len--;
    }
    while (out + 1 < dst_len && out < src_len) {
        dst[out] = (char)src[out];
        out++;
    }
    dst[out] = '\0';
}

static void parse_exfat_boot_sector(struct blockfs_mount *mount, const uint8_t *sector) {
    zero_bytes(&mount->exfat, sizeof(mount->exfat));

    if (!bytes_equal(sector + 3, "EXFAT   ", 8) ||
        sector[510] != 0x55 || sector[511] != 0xaa) {
        return;
    }

    mount->exfat.present = 1;
    mount->exfat.partition_offset = read_le64(sector + 64);
    mount->exfat.volume_length = read_le64(sector + 72);
    mount->exfat.fat_offset = read_le32(sector + 80);
    mount->exfat.fat_length = read_le32(sector + 84);
    mount->exfat.cluster_heap_offset = read_le32(sector + 88);
    mount->exfat.cluster_count = read_le32(sector + 92);
    mount->exfat.root_dir_cluster = read_le32(sector + 96);
    mount->exfat.volume_serial = read_le32(sector + 100);
    mount->exfat.revision = read_le16(sector + 104);
    mount->exfat.flags = read_le16(sector + 106);
    mount->exfat.bytes_per_sector_shift = sector[108];
    mount->exfat.sectors_per_cluster_shift = sector[109];
    mount->exfat.number_of_fats = sector[110];
    mount->exfat.drive_select = sector[111];
    mount->exfat.percent_in_use = sector[112];
}

static void parse_fat32_boot_sector(struct blockfs_mount *mount, const uint8_t *sector) {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint16_t fat_size_16;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint32_t root_dir_cluster;

    zero_bytes(&mount->fat32, sizeof(mount->fat32));
    if (!bytes_equal(sector + 82, "FAT32   ", 8) ||
        sector[510] != 0x55 || sector[511] != 0xaa) {
        return;
    }

    bytes_per_sector = read_le16(sector + 11);
    sectors_per_cluster = sector[13];
    reserved_sectors = read_le16(sector + 14);
    number_of_fats = sector[16];
    root_entry_count = read_le16(sector + 17);
    total_sectors_16 = read_le16(sector + 19);
    fat_size_16 = read_le16(sector + 22);
    total_sectors_32 = read_le32(sector + 32);
    fat_size_32 = read_le32(sector + 36);
    root_dir_cluster = read_le32(sector + 44);

    if (bytes_per_sector != mount->block.block_size ||
        sectors_per_cluster == 0 ||
        reserved_sectors == 0 ||
        number_of_fats == 0 ||
        root_entry_count != 0 ||
        total_sectors_16 != 0 ||
        total_sectors_32 == 0 ||
        fat_size_16 != 0 ||
        fat_size_32 == 0 ||
        root_dir_cluster < 2) {
        return;
    }

    mount->fat32.present = 1;
    mount->fat32.bytes_per_sector = bytes_per_sector;
    mount->fat32.sectors_per_cluster = sectors_per_cluster;
    mount->fat32.reserved_sectors = reserved_sectors;
    mount->fat32.number_of_fats = number_of_fats;
    mount->fat32.total_sectors = total_sectors_32;
    mount->fat32.fat_size_sectors = fat_size_32;
    mount->fat32.root_dir_cluster = root_dir_cluster;
    mount->fat32.fsinfo_sector = read_le16(sector + 48);
    mount->fat32.backup_boot_sector = read_le16(sector + 50);
    mount->fat32.hidden_sectors = read_le32(sector + 28);
    mount->fat32.volume_id = read_le32(sector + 67);
    copy_padded_text(mount->fat32.volume_label, sizeof(mount->fat32.volume_label),
                     sector + 71, 11);
}

static void append_char(char *buffer, uint64_t buffer_len, uint64_t *pos, char c) {
    if (*pos + 1 >= buffer_len) {
        return;
    }
    buffer[*pos] = c;
    (*pos)++;
    buffer[*pos] = '\0';
}

static void append_string(char *buffer, uint64_t buffer_len, uint64_t *pos, const char *s) {
    while (s != 0 && *s != '\0') {
        append_char(buffer, buffer_len, pos, *s);
        s++;
    }
}

static void append_u64(char *buffer, uint64_t buffer_len, uint64_t *pos, uint64_t value) {
    char digits[20];
    uint64_t count = 0;

    if (value == 0) {
        append_char(buffer, buffer_len, pos, '0');
        return;
    }

    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        append_char(buffer, buffer_len, pos, digits[--count]);
    }
}

static void append_hex_nibble(char *buffer, uint64_t buffer_len, uint64_t *pos, uint8_t value) {
    value &= 0xf;
    append_char(buffer, buffer_len, pos,
                value < 10 ? (char)('0' + value) : (char)('a' + value - 10));
}

static void append_hex_byte(char *buffer, uint64_t buffer_len, uint64_t *pos, uint8_t value) {
    append_hex_nibble(buffer, buffer_len, pos, (uint8_t)(value >> 4));
    append_hex_nibble(buffer, buffer_len, pos, value);
}

static void append_hex_bytes(char *buffer, uint64_t buffer_len, uint64_t *pos,
                             const uint8_t *data, uint64_t data_len) {
    for (uint64_t i = 0; i < data_len; i++) {
        append_hex_byte(buffer, buffer_len, pos, data[i]);
    }
}

static uint64_t one_shifted_by(uint8_t shift) {
    if (shift >= 63) {
        return 0;
    }
    return 1ULL << shift;
}

static int exfat_geometry_valid(const struct blockfs_mount *mount) {
    uint64_t bytes_per_sector = one_shifted_by(mount->exfat.bytes_per_sector_shift);
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);

    return mount->exfat.present &&
           bytes_per_sector == mount->block.block_size &&
           sectors_per_cluster > 0 &&
           mount->exfat.cluster_heap_offset > 0 &&
           mount->exfat.root_dir_cluster >= 2 &&
           mount->exfat.root_dir_cluster < mount->exfat.cluster_count + 2;
}

static uint64_t exfat_cluster_lba(const struct blockfs_mount *mount, uint32_t cluster) {
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);

    if (!exfat_geometry_valid(mount) ||
        cluster < 2 ||
        cluster >= mount->exfat.cluster_count + 2) {
        return (uint64_t)-1;
    }

    return (uint64_t)mount->exfat.cluster_heap_offset +
           ((uint64_t)cluster - 2) * sectors_per_cluster;
}

static int exfat_cluster_valid(const struct blockfs_mount *mount, uint32_t cluster) {
    return exfat_geometry_valid(mount) &&
           cluster >= 2 &&
           cluster < mount->exfat.cluster_count + 2;
}

static int exfat_read_fat_entry(struct blockfs_mount *mount, uint32_t cluster, uint32_t *next) {
    uint8_t sector[512];
    uint64_t fat_byte_offset;
    uint64_t fat_sector;
    uint64_t sector_offset;

    if (!exfat_cluster_valid(mount, cluster) || next == 0) {
        return -1;
    }

    fat_byte_offset = (uint64_t)cluster * 4;
    fat_sector = (uint64_t)mount->exfat.fat_offset + fat_byte_offset / sizeof(sector);
    sector_offset = fat_byte_offset % sizeof(sector);
    if (fat_sector >= (uint64_t)mount->exfat.fat_offset + mount->exfat.fat_length ||
        sector_offset + 4 > sizeof(sector) ||
        block_read(mount->block_index, fat_sector, 1, sector) != 0) {
        return -1;
    }

    *next = read_le32(sector + sector_offset);
    return 0;
}

static int exfat_resolve_cluster(struct blockfs_mount *mount, uint32_t first_cluster,
                                 uint8_t stream_flags, uint64_t cluster_index,
                                 uint32_t *cluster_out) {
    uint32_t cluster = first_cluster;

    if (!exfat_cluster_valid(mount, cluster) || cluster_out == 0) {
        return -1;
    }

    if ((stream_flags & 0x02) != 0) {
        cluster += (uint32_t)cluster_index;
        if (!exfat_cluster_valid(mount, cluster)) {
            return -1;
        }
        *cluster_out = cluster;
        return 0;
    }

    for (uint64_t i = 0; i < cluster_index; i++) {
        uint32_t next = 0;
        if (exfat_read_fat_entry(mount, cluster, &next) != 0 ||
            next >= 0xfffffff8 ||
            !exfat_cluster_valid(mount, next)) {
            return -1;
        }
        cluster = next;
    }

    *cluster_out = cluster;
    return 0;
}

static void reset_exfat_root_entries(struct blockfs_mount *mount) {
    mount->root_entry_count = 0;
    for (uint64_t i = 0; i < BLOCKFS_MAX_EXFAT_ROOT_ENTRIES; i++) {
        mount->root_entries[i].parent[0] = '\0';
        mount->root_entries[i].name[0] = '\0';
        mount->root_entries[i].size = 0;
        mount->root_entries[i].capacity = 0;
        mount->root_entries[i].directory_lba = 0;
        mount->root_entries[i].directory_byte_offset = 0;
        mount->root_entries[i].first_cluster = 0;
        mount->root_entries[i].attributes = 0;
        mount->root_entries[i].stream_flags = 0;
        mount->root_entries[i].is_dir = 0;
        mount->root_entries[i].writable = 0;
    }
}

static int join_child_path(char *out, uint64_t out_len, const char *parent, const char *name) {
    uint64_t pos = 0;

    if (out_len == 0 || parent == 0 || name == 0 || name[0] == '\0') {
        return -1;
    }

    if (chars_equal(parent, "/")) {
        if (out_len < 2) {
            return -1;
        }
        out[pos++] = '/';
    } else {
        while (parent[pos] != '\0' && pos + 1 < out_len) {
            out[pos] = parent[pos];
            pos++;
        }
        if (parent[pos] != '\0' || pos + 2 >= out_len) {
            return -1;
        }
        out[pos++] = '/';
    }

    for (uint64_t i = 0; name[i] != '\0'; i++) {
        if (pos + 1 >= out_len) {
            return -1;
        }
        out[pos++] = name[i];
    }
    out[pos] = '\0';
    return 0;
}

static void parse_exfat_allocation_bitmap_entry(struct blockfs_mount *mount,
                                                const uint8_t *entry,
                                                const char *parent) {
    if (!chars_equal(parent, "/")) {
        return;
    }

    mount->exfat.allocation_bitmap_present = 1;
    mount->exfat.allocation_bitmap_flags = entry[1];
    mount->exfat.allocation_bitmap_cluster = read_le32(entry + 20);
    mount->exfat.allocation_bitmap_size = read_le64(entry + 24);

    log_info("exFAT allocation bitmap: cluster=%u size=%u flags=%u\n",
             mount->exfat.allocation_bitmap_cluster,
             mount->exfat.allocation_bitmap_size,
             mount->exfat.allocation_bitmap_flags);
}

static void parse_exfat_upcase_table_entry(struct blockfs_mount *mount,
                                           const uint8_t *entry,
                                           const char *parent) {
    if (!chars_equal(parent, "/")) {
        return;
    }

    mount->exfat.upcase_table_present = 1;
    mount->exfat.upcase_table_checksum = read_le32(entry + 4);
    mount->exfat.upcase_table_cluster = read_le32(entry + 20);
    mount->exfat.upcase_table_size = read_le64(entry + 24);

    log_info("exFAT upcase table: cluster=%u size=%u checksum=%u\n",
             mount->exfat.upcase_table_cluster,
             mount->exfat.upcase_table_size,
             mount->exfat.upcase_table_checksum);
}

static void append_exfat_name_chars(char *name, uint64_t name_len,
                                    const uint8_t *entry, uint64_t expected_chars,
                                    uint64_t *name_pos) {
    for (uint64_t i = 0; i < 15 && *name_pos < expected_chars; i++) {
        uint8_t low = entry[2 + i * 2];
        uint8_t high = entry[3 + i * 2];
        char c = '?';

        if (low == 0 && high == 0) {
            break;
        }
        if (high == 0 && low >= 32 && low < 127) {
            c = (char)low;
        }

        if (*name_pos + 1 < name_len) {
            name[*name_pos] = c;
            (*name_pos)++;
            name[*name_pos] = '\0';
        }
    }
}

static struct exfat_root_entry *add_exfat_root_entry(struct blockfs_mount *mount,
                                                     const char *parent,
                                                     const char *name,
                                                     uint64_t size,
                                                     uint32_t first_cluster,
                                                     uint16_t attributes,
                                                     uint8_t stream_flags) {
    if (mount->root_entry_count >= BLOCKFS_MAX_EXFAT_ROOT_ENTRIES ||
        parent == 0 || name[0] == '\0') {
        return 0;
    }

    struct exfat_root_entry *entry = &mount->root_entries[mount->root_entry_count++];
    copy_limited(entry->parent, sizeof(entry->parent), parent);
    copy_limited(entry->name, sizeof(entry->name), name);
    entry->size = size;
    entry->capacity = size;
    entry->directory_lba = 0;
    entry->directory_byte_offset = 0;
    entry->first_cluster = first_cluster;
    entry->attributes = attributes;
    entry->stream_flags = stream_flags;
    entry->is_dir = (attributes & 0x10) != 0;
    entry->writable = 0;

    log_info("blockfs entry: parent=%s name=%s size=%u cluster=%u dir=%u\n",
             entry->parent,
             entry->name,
             entry->size,
             (uint64_t)entry->first_cluster,
             (uint64_t)entry->is_dir);
    return entry;
}

static int parse_exfat_directory_sector(struct blockfs_mount *mount, const uint8_t *sector,
                                        const char *parent, uint64_t *stop) {
    uint64_t slots = 512 / EXFAT_ENTRY_SIZE;
    for (uint64_t slot = 0; slot < slots;) {
        const uint8_t *entry = sector + slot * EXFAT_ENTRY_SIZE;
        uint8_t type = entry[0];

        if (type == 0x00) {
            *stop = 1;
            return 0;
        }
        if (type == 0x81) {
            parse_exfat_allocation_bitmap_entry(mount, entry, parent);
            slot++;
            continue;
        }
        if (type == 0x82) {
            parse_exfat_upcase_table_entry(mount, entry, parent);
            slot++;
            continue;
        }
        if (type != 0x85 || (type & 0x80) == 0) {
            slot++;
            continue;
        }

        uint64_t secondary_count = entry[1];
        uint16_t attributes = read_le16(entry + 4);
        uint64_t name_len = 0;
        uint64_t name_pos = 0;
        uint64_t size = 0;
        uint32_t first_cluster = 0;
        uint8_t stream_flags = 0;
        char name[VFS_DIRENT_NAME_MAX];

        name[0] = '\0';
        for (uint64_t j = 1; j <= secondary_count && slot + j < slots; j++) {
            const uint8_t *secondary = sector + (slot + j) * EXFAT_ENTRY_SIZE;
            if (secondary[0] == 0xc0) {
                stream_flags = secondary[1];
                name_len = secondary[3];
                first_cluster = read_le32(secondary + 20);
                size = read_le64(secondary + 24);
            } else if (secondary[0] == 0xc1 && name_len > 0) {
                append_exfat_name_chars(name, sizeof(name), secondary, name_len, &name_pos);
            }
        }

        add_exfat_root_entry(mount, parent, name, size, first_cluster, attributes, stream_flags);
        slot += secondary_count + 1;
    }
    return 0;
}

static int exfat_directory_slot_free(const uint8_t *entry) {
    return entry[0] == 0x00 || (entry[0] & 0x80) == 0;
}

static uint16_t exfat_checksum_step(uint16_t checksum, uint8_t value) {
    return (uint16_t)(((checksum & 1) != 0 ? 0x8000 : 0) +
                      (checksum >> 1) +
                      value);
}

static uint16_t exfat_ascii_utf16_name_hash(const char *name) {
    uint16_t hash = 0;
    for (uint64_t i = 0; name[i] != '\0'; i++) {
        hash = exfat_checksum_step(hash, (uint8_t)name[i]);
        hash = exfat_checksum_step(hash, 0);
    }
    return hash;
}

static uint16_t exfat_entry_set_checksum(const uint8_t *entries, uint64_t entry_count) {
    uint16_t checksum = 0;
    uint64_t total = entry_count * EXFAT_ENTRY_SIZE;

    for (uint64_t i = 0; i < total; i++) {
        if (i == 2 || i == 3) {
            continue;
        }
        checksum = exfat_checksum_step(checksum, entries[i]);
    }
    return checksum;
}

static void encode_exfat_create_plan_entries(struct blockfs_mount *mount) {
    uint8_t *file_entry = mount->exfat.create_plan_entries;
    uint8_t *stream_entry = file_entry + EXFAT_ENTRY_SIZE;
    uint8_t *name_entry = stream_entry + EXFAT_ENTRY_SIZE;
    uint64_t cluster_size = one_shifted_by(mount->exfat.sectors_per_cluster_shift) *
                            mount->block.block_size;
    uint16_t checksum;

    mount->exfat.create_plan_file_size = 0;
    mount->exfat.create_plan_first_cluster = 0;
    mount->exfat.create_plan_name_hash = 0;
    mount->exfat.create_plan_entry_set_checksum = 0;
    mount->exfat.create_plan_entries_ready = 0;
    zero_bytes(mount->exfat.create_plan_entries, sizeof(mount->exfat.create_plan_entries));

    if (!mount->exfat.create_plan_ready ||
        !mount->exfat.allocation_plan_ready ||
        cluster_size == 0) {
        return;
    }

    mount->exfat.create_plan_first_cluster = mount->exfat.allocation_plan_clusters[0];
    mount->exfat.create_plan_file_size = cluster_size * EXFAT_ALLOC_PLAN_CLUSTER_COUNT;
    mount->exfat.create_plan_name_hash = exfat_ascii_utf16_name_hash(EXFAT_CREATE_PLAN_NAME);

    file_entry[0] = 0x85;
    file_entry[1] = 2;
    write_le16(file_entry + 4, 0x20);

    stream_entry[0] = 0xc0;
    stream_entry[1] = 0;
    stream_entry[3] = EXFAT_CREATE_PLAN_NAME_CHARS;
    write_le16(stream_entry + 4, mount->exfat.create_plan_name_hash);
    write_le64(stream_entry + 8, mount->exfat.create_plan_file_size);
    write_le32(stream_entry + 20, mount->exfat.create_plan_first_cluster);
    write_le64(stream_entry + 24, mount->exfat.create_plan_file_size);

    name_entry[0] = 0xc1;
    for (uint64_t i = 0; i < EXFAT_CREATE_PLAN_NAME_CHARS; i++) {
        name_entry[2 + i * 2] = (uint8_t)EXFAT_CREATE_PLAN_NAME[i];
        name_entry[3 + i * 2] = 0;
    }

    checksum = exfat_entry_set_checksum(mount->exfat.create_plan_entries,
                                        EXFAT_CREATE_PLAN_REQUIRED_ENTRIES);
    mount->exfat.create_plan_entry_set_checksum = checksum;
    write_le16(file_entry + 2, checksum);
    mount->exfat.create_plan_entries_ready = 1;

    log_info("exFAT directory entry bytes planned: path=/%s first_cluster=%u size=%u checksum=%u name_hash=%u\n",
             EXFAT_CREATE_PLAN_NAME,
             mount->exfat.create_plan_first_cluster,
             mount->exfat.create_plan_file_size,
             mount->exfat.create_plan_entry_set_checksum,
             mount->exfat.create_plan_name_hash);
}

static void reset_exfat_transaction_plan(struct blockfs_mount *mount) {
    mount->exfat.transaction_plan_ready = 0;
    mount->exfat.transaction_plan_write_count = 0;
    mount->exfat.transaction_bitmap_lba = 0;
    mount->exfat.transaction_bitmap_byte_offset = 0;
    mount->exfat.transaction_bitmap_old_value = 0;
    mount->exfat.transaction_bitmap_new_value = 0;
    mount->exfat.transaction_bitmap_mask = 0;
    mount->exfat.transaction_fat_lba = 0;
    mount->exfat.transaction_directory_lba = 0;
    mount->exfat.transaction_directory_byte_offset = 0;
    mount->exfat.transaction_directory_byte_count = 0;
    mount->exfat.patched_transaction_ready = 0;
    mount->exfat.patched_bitmap_value = 0;
    mount->exfat.patched_directory_checksum = 0;
    mount->exfat.patched_directory_first_cluster = 0;
    mount->exfat.patched_directory_file_size = 0;
    mount->exfat.patched_directory_name_length = 0;
    mount->exfat.patched_directory_name_matches = 0;
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    mount->exfat.commit_supported = 1;
#else
    mount->exfat.commit_supported = 0;
#endif
    mount->exfat.commit_attempted = 0;
    mount->exfat.commit_ready = 0;
    mount->exfat.commit_write_count = 0;
    mount->exfat.commit_verified = 0;
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        mount->exfat.transaction_fat_offsets[i] = 0;
        mount->exfat.transaction_fat_old_values[i] = 0;
        mount->exfat.transaction_fat_new_values[i] = 0;
        mount->exfat.patched_fat_values[i] = 0;
    }
}

static int exfat_patched_directory_name_matches(const uint8_t *name_entry) {
    for (uint64_t i = 0; i < EXFAT_CREATE_PLAN_NAME_CHARS; i++) {
        if (name_entry[2 + i * 2] != (uint8_t)EXFAT_CREATE_PLAN_NAME[i] ||
            name_entry[3 + i * 2] != 0) {
            return 0;
        }
    }

    return 1;
}

static int apply_exfat_create_transaction_to_sectors(struct blockfs_mount *mount,
                                                     uint8_t *bitmap_sector,
                                                     uint8_t *fat_sector,
                                                     uint8_t *directory_sector) {
    if (mount == 0 || bitmap_sector == 0 || fat_sector == 0 || directory_sector == 0 ||
        !mount->exfat.transaction_plan_ready ||
        mount->exfat.transaction_directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES > 512) {
        return -1;
    }

    bitmap_sector[mount->exfat.transaction_bitmap_byte_offset] =
        mount->exfat.transaction_bitmap_new_value;

    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        uint64_t offset = mount->exfat.transaction_fat_offsets[i];
        if (offset + 4 > 512) {
            return -1;
        }
        write_le32(fat_sector + offset, mount->exfat.transaction_fat_new_values[i]);
    }

    for (uint64_t i = 0; i < EXFAT_CREATE_PLAN_ENTRY_BYTES; i++) {
        directory_sector[mount->exfat.transaction_directory_byte_offset + i] =
            mount->exfat.create_plan_entries[i];
    }

    return 0;
}

static int verify_patched_exfat_create_transaction(struct blockfs_mount *mount,
                                                   const uint8_t *bitmap_sector,
                                                   const uint8_t *fat_sector,
                                                   const uint8_t *directory_sector) {
    const uint8_t *file_entry;
    const uint8_t *stream_entry;
    const uint8_t *name_entry;

    if (mount == 0 || bitmap_sector == 0 || fat_sector == 0 || directory_sector == 0 ||
        mount->exfat.transaction_directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES > 512) {
        return -1;
    }

    file_entry = directory_sector + mount->exfat.transaction_directory_byte_offset;
    stream_entry = file_entry + EXFAT_ENTRY_SIZE;
    name_entry = stream_entry + EXFAT_ENTRY_SIZE;

    mount->exfat.patched_bitmap_value =
        bitmap_sector[mount->exfat.transaction_bitmap_byte_offset];

    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        uint64_t offset = mount->exfat.transaction_fat_offsets[i];
        if (offset + 4 > 512) {
            return -1;
        }
        mount->exfat.patched_fat_values[i] = read_le32(fat_sector + offset);
    }

    if (file_entry[0] != 0x85 || stream_entry[0] != 0xc0 || name_entry[0] != 0xc1) {
        return -1;
    }

    mount->exfat.patched_directory_checksum = read_le16(file_entry + 2);
    mount->exfat.patched_directory_first_cluster = read_le32(stream_entry + 20);
    mount->exfat.patched_directory_file_size = read_le64(stream_entry + 24);
    mount->exfat.patched_directory_name_length = stream_entry[3];
    mount->exfat.patched_directory_name_matches =
        exfat_patched_directory_name_matches(name_entry);

    if (mount->exfat.patched_bitmap_value != mount->exfat.transaction_bitmap_new_value ||
        mount->exfat.patched_fat_values[0] != mount->exfat.allocation_plan_clusters[1] ||
        mount->exfat.patched_fat_values[1] != mount->exfat.allocation_plan_clusters[2] ||
        mount->exfat.patched_fat_values[2] != 0xffffffffU ||
        mount->exfat.patched_directory_checksum !=
            mount->exfat.create_plan_entry_set_checksum ||
        mount->exfat.patched_directory_first_cluster !=
            mount->exfat.create_plan_first_cluster ||
        mount->exfat.patched_directory_file_size != mount->exfat.create_plan_file_size ||
        mount->exfat.patched_directory_name_length != EXFAT_CREATE_PLAN_NAME_CHARS ||
        !mount->exfat.patched_directory_name_matches) {
        return -1;
    }

    return 0;
}

static void dry_run_exfat_create_transaction(struct blockfs_mount *mount) {
    uint8_t bitmap_sector[512];
    uint8_t fat_sector[512];
    uint8_t directory_sector[512];

    mount->exfat.patched_transaction_ready = 0;
    if (!mount->exfat.transaction_plan_ready ||
        mount->exfat.transaction_directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES >
            sizeof(directory_sector) ||
        block_read(mount->block_index, mount->exfat.transaction_bitmap_lba, 1, bitmap_sector) != 0 ||
        block_read(mount->block_index, mount->exfat.transaction_fat_lba, 1, fat_sector) != 0 ||
        block_read(mount->block_index, mount->exfat.transaction_directory_lba, 1,
                   directory_sector) != 0) {
        return;
    }

    if (apply_exfat_create_transaction_to_sectors(mount, bitmap_sector, fat_sector,
                                                  directory_sector) != 0 ||
        verify_patched_exfat_create_transaction(mount, bitmap_sector, fat_sector,
                                                directory_sector) != 0) {
        return;
    }

    mount->exfat.patched_transaction_ready = 1;

    log_info("exFAT write transaction dry-run OK: path=/%s bitmap=%u fat=%u,%u,EOF dir_cluster=%u size=%u\n",
             EXFAT_CREATE_PLAN_NAME,
             mount->exfat.patched_bitmap_value,
             mount->exfat.patched_fat_values[0],
             mount->exfat.patched_fat_values[1],
             mount->exfat.patched_directory_first_cluster,
             mount->exfat.patched_directory_file_size);
}

static void commit_exfat_create_transaction_for_test(struct blockfs_mount *mount) {
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    struct exfat_sector_transaction tx;
    uint8_t *bitmap_sector;
    uint8_t *fat_sector;
    uint8_t *directory_sector;

    mount->exfat.commit_attempted = 1;
    exfat_sector_transaction_begin_for_test(&tx, mount);
    bitmap_sector = exfat_sector_transaction_get_for_test(&tx,
                                                          mount->exfat.transaction_bitmap_lba);
    fat_sector = exfat_sector_transaction_get_for_test(&tx,
                                                       mount->exfat.transaction_fat_lba);
    directory_sector = exfat_sector_transaction_get_for_test(
        &tx,
        mount->exfat.transaction_directory_lba);
    if (!mount->exfat.patched_transaction_ready ||
        bitmap_sector == 0 ||
        fat_sector == 0 ||
        directory_sector == 0 ||
        apply_exfat_create_transaction_to_sectors(mount, bitmap_sector, fat_sector,
                                                  directory_sector) != 0) {
        return;
    }

    if (exfat_sector_transaction_commit_for_test(&tx) != 0) {
        return;
    }
    mount->exfat.commit_write_count = tx.count;

    if (verify_patched_exfat_create_transaction(mount, bitmap_sector, fat_sector,
                                                directory_sector) != 0) {
        return;
    }

    mount->exfat.commit_verified = 1;
    mount->exfat.commit_ready = 1;
    struct exfat_root_entry *entry = add_exfat_root_entry(mount, "/", EXFAT_CREATE_PLAN_NAME, 0,
                                                          mount->exfat.create_plan_first_cluster,
                                                          0x20, 0);
    if (entry != 0) {
        entry->capacity = mount->exfat.create_plan_file_size;
        entry->directory_lba = mount->exfat.transaction_directory_lba;
        entry->directory_byte_offset = mount->exfat.transaction_directory_byte_offset;
        entry->writable = 1;
        (void)refresh_exfat_test_file_size_for_test(mount, entry, 0);
    }
    log_info("exFAT test commit verified: source=%s path=/%s writes=%u bitmap=%u fat=%u,%u,EOF\n",
             mount->source,
             EXFAT_CREATE_PLAN_NAME,
             mount->exfat.commit_write_count,
             mount->exfat.patched_bitmap_value,
             mount->exfat.patched_fat_values[0],
             mount->exfat.patched_fat_values[1]);
#else
    (void)mount;
#endif
}

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
static struct exfat_root_entry *find_exfat_test_file_entry(struct blockfs_mount *mount) {
    if (mount == 0) {
        return 0;
    }
    for (uint64_t i = 0; i < mount->root_entry_count; i++) {
        struct exfat_root_entry *entry = &mount->root_entries[i];
        if (entry->writable &&
            chars_equal(entry->parent, "/") &&
            !entry->is_dir) {
            return entry;
        }
    }
    return 0;
}

static int exfat_test_name_char_valid(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static int parse_exfat_test_short_name(const char *path, char *name, uint64_t name_len) {
    uint64_t i = 0;
    uint64_t out = 0;
    uint64_t base_len = 0;
    uint64_t ext_len = 0;
    int seen_dot = 0;

    if (path == 0 || name == 0 || name_len == 0 || path[0] != '/') {
        return -1;
    }
    i = 1;
    while (path[i] != '\0') {
        char c = path[i];
        if (c == '/') {
            return -1;
        }
        if (c == '.') {
            if (seen_dot || base_len == 0) {
                return -1;
            }
            seen_dot = 1;
        } else if (!exfat_test_name_char_valid(c)) {
            return -1;
        } else if (seen_dot) {
            ext_len++;
            if (ext_len > 3) {
                return -1;
            }
        } else {
            base_len++;
            if (base_len > 8) {
                return -1;
            }
        }

        if (out + 1 >= name_len || out >= EXFAT_CREATE_PLAN_MAX_NAME_CHARS) {
            return -1;
        }
        name[out++] = c;
        i++;
    }
    if (base_len == 0 || (seen_dot && ext_len == 0)) {
        return -1;
    }
    name[out] = '\0';
    return 0;
}

static void encode_exfat_name_entry(uint8_t *name_entry, const char *name) {
    zero_bytes(name_entry, EXFAT_ENTRY_SIZE);
    name_entry[0] = 0xc1;
    for (uint64_t i = 0; name[i] != '\0' && i < EXFAT_CREATE_PLAN_MAX_NAME_CHARS; i++) {
        name_entry[2 + i * 2] = (uint8_t)name[i];
        name_entry[3 + i * 2] = 0;
    }
}

static int refresh_exfat_test_file_size_for_test(struct blockfs_mount *mount,
                                                 struct exfat_root_entry *entry,
                                                 uint64_t size) {
    uint8_t sector[512];
    uint8_t *file_entry;
    uint8_t *stream_entry;
    uint16_t checksum;

    if (mount == 0 || !mount->exfat.commit_ready ||
        entry == 0 || !entry->writable ||
        entry->directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES > sizeof(sector)) {
        return -1;
    }

    if (size > entry->capacity ||
        block_read(mount->block_index, entry->directory_lba, 1, sector) != 0) {
        return -1;
    }

    file_entry = sector + entry->directory_byte_offset;
    stream_entry = file_entry + EXFAT_ENTRY_SIZE;
    if (file_entry[0] != 0x85 || stream_entry[0] != 0xc0) {
        return -1;
    }

    write_le64(stream_entry + 8, size);
    write_le64(stream_entry + 24, size);
    write_le16(file_entry + 2, 0);
    checksum = exfat_entry_set_checksum(file_entry, EXFAT_CREATE_PLAN_REQUIRED_ENTRIES);
    write_le16(file_entry + 2, checksum);

    if (write_exfat_sector_checked_for_test(mount, entry->directory_lba, sector) != 0) {
        return -1;
    }

    entry->size = size;
    return 0;
}

static int rename_exfat_test_file_for_test(struct blockfs_mount *mount, const char *path) {
    uint8_t sector[512];
    uint8_t *file_entry;
    uint8_t *stream_entry;
    uint8_t *name_entry;
    uint16_t checksum;
    char name[VFS_DIRENT_NAME_MAX];
    uint64_t len;
    struct exfat_root_entry *entry;

    if (mount == 0 || path == 0 || !mount->exfat.commit_ready ||
        parse_exfat_test_short_name(path, name, sizeof(name)) != 0 ||
        mount->exfat.transaction_directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES >
            sizeof(sector)) {
        return -1;
    }
    if (find_exfat_root_entry(mount, path) != 0) {
        return 0;
    }

    entry = find_exfat_test_file_entry(mount);
    if (entry == 0 || entry->size != 0 ||
        !chars_equal(entry->name, EXFAT_CREATE_PLAN_NAME) ||
        entry->directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES > sizeof(sector) ||
        block_read(mount->block_index, entry->directory_lba, 1, sector) != 0) {
        return -1;
    }

    file_entry = sector + entry->directory_byte_offset;
    stream_entry = file_entry + EXFAT_ENTRY_SIZE;
    name_entry = stream_entry + EXFAT_ENTRY_SIZE;
    if (file_entry[0] != 0x85 || stream_entry[0] != 0xc0 || name_entry[0] != 0xc1) {
        return -1;
    }

    len = cstr_length(name);
    stream_entry[3] = (uint8_t)len;
    write_le16(stream_entry + 4, exfat_ascii_utf16_name_hash(name));
    encode_exfat_name_entry(name_entry, name);
    write_le16(file_entry + 2, 0);
    checksum = exfat_entry_set_checksum(file_entry, EXFAT_CREATE_PLAN_REQUIRED_ENTRIES);
    write_le16(file_entry + 2, checksum);

    if (write_exfat_sector_checked_for_test(mount, entry->directory_lba, sector) != 0) {
        return -1;
    }

    copy_limited(entry->name, sizeof(entry->name), name);
    log_info("exFAT test file renamed: source=%s path=/%s checksum=%u\n",
             mount->source, name, checksum);
    return 0;
}

static void encode_exfat_file_entries_for_test(uint8_t *entries, const char *name,
                                               uint32_t first_cluster, uint64_t size) {
    uint8_t *file_entry = entries;
    uint8_t *stream_entry = file_entry + EXFAT_ENTRY_SIZE;
    uint8_t *name_entry = stream_entry + EXFAT_ENTRY_SIZE;
    uint16_t checksum;

    zero_bytes(entries, EXFAT_CREATE_PLAN_ENTRY_BYTES);
    file_entry[0] = 0x85;
    file_entry[1] = 2;
    write_le16(file_entry + 4, 0x20);

    stream_entry[0] = 0xc0;
    stream_entry[1] = 0;
    stream_entry[3] = (uint8_t)cstr_length(name);
    write_le16(stream_entry + 4, exfat_ascii_utf16_name_hash(name));
    write_le64(stream_entry + 8, size);
    write_le32(stream_entry + 20, first_cluster);
    write_le64(stream_entry + 24, size);
    encode_exfat_name_entry(name_entry, name);

    checksum = exfat_entry_set_checksum(entries, EXFAT_CREATE_PLAN_REQUIRED_ENTRIES);
    write_le16(file_entry + 2, checksum);
}

static int locate_exfat_bitmap_bit_for_test(struct blockfs_mount *mount,
                                            uint32_t cluster,
                                            uint64_t *lba_out,
                                            uint64_t *sector_offset_out,
                                            uint8_t *mask_out);

static int read_exfat_bitmap_bit_for_test(struct blockfs_mount *mount, uint32_t cluster,
                                          int *used_out) {
    uint8_t sector[512];
    uint64_t lba;
    uint64_t sector_offset;
    uint8_t mask;

    if (used_out == 0 ||
        locate_exfat_bitmap_bit_for_test(mount, cluster, &lba, &sector_offset, &mask) != 0 ||
        block_read(mount->block_index, lba, 1, sector) != 0) {
        return -1;
    }

    *used_out = (sector[sector_offset] & mask) != 0;
    return 0;
}

static int locate_exfat_bitmap_bit_for_test(struct blockfs_mount *mount,
                                            uint32_t cluster,
                                            uint64_t *lba_out,
                                            uint64_t *sector_offset_out,
                                            uint8_t *mask_out) {
    uint64_t sectors_per_cluster = mount == 0
        ? 0
        : one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t cluster_size = sectors_per_cluster * 512;
    uint64_t bit = (uint64_t)cluster - 2;
    uint64_t byte_index = bit / 8;
    uint64_t byte_offset_in_bitmap_cluster;
    uint64_t bitmap_cluster_index;
    uint64_t bitmap_sector_index;
    uint64_t sector_offset;
    uint32_t bitmap_cluster = 0;
    uint64_t lba;

    if (mount == 0 || lba_out == 0 || sector_offset_out == 0 || mask_out == 0 ||
        !exfat_cluster_valid(mount, cluster) ||
        !mount->exfat.allocation_bitmap_present ||
        cluster_size == 0 ||
        byte_index >= mount->exfat.allocation_bitmap_size ||
        mount->block.block_size != 512) {
        return -1;
    }

    byte_offset_in_bitmap_cluster = byte_index % cluster_size;
    bitmap_cluster_index = byte_index / cluster_size;
    bitmap_sector_index = byte_offset_in_bitmap_cluster / 512;
    sector_offset = byte_offset_in_bitmap_cluster % 512;
    if (exfat_resolve_cluster(mount, mount->exfat.allocation_bitmap_cluster, 0,
                              bitmap_cluster_index, &bitmap_cluster) != 0) {
        return -1;
    }

    lba = exfat_cluster_lba(mount, bitmap_cluster);
    if (lba == (uint64_t)-1 ||
        bitmap_sector_index >= sectors_per_cluster) {
        return -1;
    }

    *lba_out = lba + bitmap_sector_index;
    *sector_offset_out = sector_offset;
    *mask_out = (uint8_t)(1U << (bit % 8));
    return 0;
}

static int set_exfat_bitmap_bit_in_transaction_for_test(struct exfat_sector_transaction *tx,
                                                        uint32_t cluster) {
    struct exfat_bit_patch patch;
    uint64_t lba;
    uint64_t sector_offset;
    uint8_t mask;

    if (tx == 0 ||
        locate_exfat_bitmap_bit_for_test(tx->mount, cluster, &lba, &sector_offset, &mask) != 0) {
        return -1;
    }

    patch.offset = sector_offset;
    patch.mask = mask;
    patch.set = 1;
    return exfat_sector_transaction_mutate_for_test(tx, lba, apply_exfat_bit_patch,
                                                    &patch);
}

static int clear_exfat_bitmap_bit_in_transaction_for_test(struct exfat_sector_transaction *tx,
                                                          uint32_t cluster) {
    struct exfat_bit_patch patch;
    uint64_t lba;
    uint64_t sector_offset;
    uint8_t mask;

    if (tx == 0 ||
        locate_exfat_bitmap_bit_for_test(tx->mount, cluster, &lba, &sector_offset, &mask) != 0) {
        return -1;
    }

    patch.offset = sector_offset;
    patch.mask = mask;
    patch.set = 0;
    return exfat_sector_transaction_mutate_for_test(tx, lba, apply_exfat_bit_patch,
                                                    &patch);
}

static int write_exfat_fat_entry_in_transaction_for_test(struct exfat_sector_transaction *tx,
                                                         uint32_t cluster,
                                                         uint32_t value) {
    struct exfat_u32_patch patch;
    uint64_t fat_byte_offset;
    uint64_t fat_sector;
    uint64_t sector_offset;

    if (tx == 0 || tx->mount == 0 || !exfat_cluster_valid(tx->mount, cluster)) {
        return -1;
    }

    fat_byte_offset = (uint64_t)cluster * 4;
    fat_sector = (uint64_t)tx->mount->exfat.fat_offset + fat_byte_offset / 512;
    sector_offset = fat_byte_offset % 512;
    if (fat_sector >= (uint64_t)tx->mount->exfat.fat_offset + tx->mount->exfat.fat_length ||
        sector_offset + 4 > 512) {
        return -1;
    }

    patch.offset = sector_offset;
    patch.value = value;
    return exfat_sector_transaction_mutate_for_test(tx, fat_sector, apply_exfat_u32_patch,
                                                    &patch);
}

static int zero_exfat_cluster_for_test(struct blockfs_mount *mount, uint32_t cluster) {
    uint8_t sector[512];
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t lba = exfat_cluster_lba(mount, cluster);

    if (mount == 0 || lba == (uint64_t)-1 || sectors_per_cluster == 0) {
        return -1;
    }
    zero_bytes(sector, sizeof(sector));
    for (uint64_t i = 0; i < sectors_per_cluster; i++) {
        if (write_exfat_sector_checked_for_test(mount, lba + i, sector) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint64_t exfat_test_cluster_count_for_size(struct blockfs_mount *mount, uint64_t size) {
    uint64_t sectors_per_cluster = mount == 0
        ? 0
        : one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t cluster_size = sectors_per_cluster * 512;
    uint64_t requested_clusters;

    if (cluster_size == 0) {
        return 0;
    }
    if (size == 0) {
        return EXFAT_TEST_FILE_DEFAULT_CLUSTERS;
    }

    if (size > UINT64_MAX - (cluster_size - 1)) {
        return 0;
    }
    requested_clusters = (size + cluster_size - 1) / cluster_size;
    if (requested_clusters == 0 ||
        requested_clusters > EXFAT_ALLOC_PLAN_CLUSTER_COUNT) {
        return 0;
    }
    return requested_clusters;
}

static int find_exfat_free_cluster_run_for_test(struct blockfs_mount *mount,
                                                uint32_t *clusters,
                                                uint64_t requested_clusters) {
    uint64_t run = 0;

    if (mount == 0 || clusters == 0 || requested_clusters == 0 ||
        requested_clusters > EXFAT_ALLOC_PLAN_CLUSTER_COUNT) {
        return -1;
    }
    for (uint32_t cluster = 2; cluster < mount->exfat.cluster_count + 2; cluster++) {
        int used = 1;
        uint32_t next = 0xffffffffU;
        if (read_exfat_bitmap_bit_for_test(mount, cluster, &used) != 0) {
            return -1;
        }
        if (!used && exfat_read_fat_entry(mount, cluster, &next) == 0 && next == 0) {
            clusters[run++] = cluster;
            if (run >= requested_clusters) {
                return 0;
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

static int find_exfat_root_directory_slot_for_test(struct blockfs_mount *mount,
                                                   uint64_t *lba_out,
                                                   uint64_t *byte_offset_out) {
    uint8_t sector[512];
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t slots_per_sector = sizeof(sector) / EXFAT_ENTRY_SIZE;
    uint64_t run = 0;
    uint64_t run_lba = 0;
    uint64_t run_slot = 0;

    if (mount == 0 || lba_out == 0 || byte_offset_out == 0 ||
        !exfat_cluster_valid(mount, mount->exfat.root_dir_cluster) ||
        mount->block.block_size != sizeof(sector) ||
        sectors_per_cluster == 0) {
        return -1;
    }

    for (uint64_t cluster_index = 0;
         cluster_index < mount->exfat.cluster_count;
         cluster_index++) {
        uint32_t cluster = 0;
        uint64_t lba;

        if (exfat_resolve_cluster(mount, mount->exfat.root_dir_cluster, 0,
                                  cluster_index, &cluster) != 0) {
            return -1;
        }
        lba = exfat_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1) {
            return -1;
        }

        for (uint64_t sector_index = 0; sector_index < sectors_per_cluster; sector_index++) {
            if (block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
                return -1;
            }
            for (uint64_t slot = 0; slot < slots_per_sector; slot++) {
                const uint8_t *entry = sector + slot * EXFAT_ENTRY_SIZE;
                if (exfat_directory_slot_free(entry)) {
                    if (run == 0) {
                        run_lba = lba + sector_index;
                        run_slot = slot;
                    }
                    run++;
                    if (run >= EXFAT_CREATE_PLAN_REQUIRED_ENTRIES) {
                        *lba_out = run_lba;
                        *byte_offset_out = run_slot * EXFAT_ENTRY_SIZE;
                        return 0;
                    }
                } else {
                    run = 0;
                }
            }
        }
    }
    return -1;
}

static int create_exfat_test_file_with_clusters_for_test(struct blockfs_mount *mount,
                                                         const char *path,
                                                         uint64_t requested_clusters) {
    struct exfat_sector_transaction tx;
    struct exfat_bytes_patch directory_patch;
    uint8_t entries[EXFAT_CREATE_PLAN_ENTRY_BYTES];
    char name[VFS_DIRENT_NAME_MAX];
    uint32_t clusters[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint64_t dir_lba = 0;
    uint64_t dir_offset = 0;
    uint64_t sectors_per_cluster;
    uint64_t capacity;
    struct exfat_root_entry *entry;

    if (mount == 0 || path == 0 || !mount->exfat.commit_ready ||
        requested_clusters == 0 ||
        requested_clusters > EXFAT_ALLOC_PLAN_CLUSTER_COUNT ||
        parse_exfat_test_short_name(path, name, sizeof(name)) != 0 ||
        find_exfat_root_entry(mount, path) != 0 ||
        find_exfat_free_cluster_run_for_test(mount, clusters, requested_clusters) != 0 ||
        find_exfat_root_directory_slot_for_test(mount, &dir_lba, &dir_offset) != 0 ||
        dir_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES > 512) {
        return -1;
    }

    sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    capacity = sectors_per_cluster * 512 * requested_clusters;
    if (capacity == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < requested_clusters; i++) {
        if (zero_exfat_cluster_for_test(mount, clusters[i]) != 0) {
            return -1;
        }
    }

    encode_exfat_file_entries_for_test(entries, name, clusters[0], 0);
    exfat_sector_transaction_begin_for_test(&tx, mount);
    for (uint64_t i = 0; i < requested_clusters; i++) {
        if (set_exfat_bitmap_bit_in_transaction_for_test(&tx, clusters[i]) != 0 ||
            write_exfat_fat_entry_in_transaction_for_test(
                &tx,
                clusters[i],
                i + 1 < requested_clusters ? clusters[i + 1] : 0xffffffffU) != 0) {
            return -1;
        }
    }
    directory_patch.offset = dir_offset;
    directory_patch.bytes = entries;
    directory_patch.size = sizeof(entries);
    if (exfat_sector_transaction_mutate_for_test(&tx, dir_lba, apply_exfat_bytes_patch,
                                                 &directory_patch) != 0 ||
        exfat_sector_transaction_commit_for_test(&tx) != 0) {
        return -1;
    }

    entry = add_exfat_root_entry(mount, "/", name, 0, clusters[0], 0x20, 0);
    if (entry == 0) {
        return -1;
    }
    entry->capacity = capacity;
    entry->directory_lba = dir_lba;
    entry->directory_byte_offset = dir_offset;
    entry->writable = 1;

    log_info("exFAT test file created: source=%s path=/%s first_cluster=%u capacity=%u\n",
             mount->source, name, (uint64_t)clusters[0], capacity);
    return 0;
}

static int mark_exfat_test_directory_entries_deleted_in_transaction(
    struct exfat_sector_transaction *tx,
    struct exfat_root_entry *entry) {
    struct exfat_delete_entries_patch patch;

    if (tx == 0 || entry == 0 || !entry->writable ||
        entry->directory_byte_offset + EXFAT_CREATE_PLAN_ENTRY_BYTES > 512) {
        return -1;
    }

    patch.offset = entry->directory_byte_offset;
    patch.entry_count = EXFAT_CREATE_PLAN_REQUIRED_ENTRIES;
    return exfat_sector_transaction_mutate_for_test(tx, entry->directory_lba,
                                                    apply_exfat_delete_entries_patch,
                                                    &patch);
}

static int collect_exfat_test_cluster_chain_for_test(struct blockfs_mount *mount,
                                                     struct exfat_root_entry *entry,
                                                     uint32_t *clusters,
                                                     uint64_t *cluster_count_out) {
    uint32_t cluster;

    if (mount == 0 || entry == 0 || clusters == 0 || cluster_count_out == 0 ||
        !entry->writable || !exfat_cluster_valid(mount, entry->first_cluster)) {
        return -1;
    }

    *cluster_count_out = 0;
    cluster = entry->first_cluster;
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        uint32_t next = 0xffffffffU;
        if (exfat_read_fat_entry(mount, cluster, &next) != 0) {
            return -1;
        }
        clusters[*cluster_count_out] = cluster;
        (*cluster_count_out)++;
        if (next >= 0xfffffff8U) {
            return 0;
        }
        if (!exfat_cluster_valid(mount, next)) {
            return -1;
        }
        cluster = next;
    }

    return 0;
}

static int release_exfat_test_cluster_chain_in_transaction(
    struct exfat_sector_transaction *tx,
    const uint32_t *clusters,
    uint64_t cluster_count) {
    if (tx == 0 || clusters == 0 || cluster_count == 0 ||
        cluster_count > EXFAT_ALLOC_PLAN_CLUSTER_COUNT) {
        return -1;
    }

    for (uint64_t i = 0; i < cluster_count; i++) {
        if (clear_exfat_bitmap_bit_in_transaction_for_test(tx, clusters[i]) != 0 ||
            write_exfat_fat_entry_in_transaction_for_test(tx, clusters[i], 0) != 0) {
            return -1;
        }
    }
    return 0;
}

static int extend_exfat_test_file_for_test(struct blockfs_mount *mount,
                                           struct exfat_root_entry *entry,
                                           uint64_t required_clusters) {
    struct exfat_sector_transaction tx;
    uint32_t current_clusters[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint32_t new_clusters[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint64_t current_count = 0;
    uint64_t add_count;
    uint64_t sectors_per_cluster;
    uint64_t new_capacity;

    if (mount == 0 || entry == 0 || !entry->writable ||
        required_clusters == 0 ||
        required_clusters > EXFAT_ALLOC_PLAN_CLUSTER_COUNT ||
        collect_exfat_test_cluster_chain_for_test(
            mount,
            entry,
            current_clusters,
            &current_count) != 0 ||
        current_count == 0 ||
        current_count > required_clusters) {
        return -1;
    }
    if (current_count == required_clusters) {
        return 0;
    }

    add_count = required_clusters - current_count;
    if (find_exfat_free_cluster_run_for_test(mount, new_clusters, add_count) != 0) {
        return -1;
    }
    for (uint64_t i = 0; i < add_count; i++) {
        if (zero_exfat_cluster_for_test(mount, new_clusters[i]) != 0) {
            return -1;
        }
    }

    exfat_sector_transaction_begin_for_test(&tx, mount);
    if (write_exfat_fat_entry_in_transaction_for_test(
            &tx,
            current_clusters[current_count - 1],
            new_clusters[0]) != 0) {
        return -1;
    }
    for (uint64_t i = 0; i < add_count; i++) {
        if (set_exfat_bitmap_bit_in_transaction_for_test(&tx, new_clusters[i]) != 0 ||
            write_exfat_fat_entry_in_transaction_for_test(
                &tx,
                new_clusters[i],
                i + 1 < add_count ? new_clusters[i + 1] : 0xffffffffU) != 0) {
            return -1;
        }
    }
    if (exfat_sector_transaction_commit_for_test(&tx) != 0) {
        return -1;
    }

    sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    new_capacity = sectors_per_cluster * 512 * required_clusters;
    if (new_capacity == 0) {
        return -1;
    }
    entry->capacity = new_capacity;

    log_info("exFAT test file extended: source=%s path=/%s clusters=%u->%u capacity=%u\n",
             mount->source,
             entry->name,
             current_count,
             required_clusters,
             new_capacity);
    return 0;
}

static void clear_exfat_root_entry(struct exfat_root_entry *entry) {
    if (entry == 0) {
        return;
    }
    entry->parent[0] = '\0';
    entry->name[0] = '\0';
    entry->size = 0;
    entry->capacity = 0;
    entry->directory_lba = 0;
    entry->directory_byte_offset = 0;
    entry->first_cluster = 0;
    entry->attributes = 0;
    entry->stream_flags = 0;
    entry->is_dir = 0;
    entry->writable = 0;
}

static int unlink_exfat_test_file_for_test(struct blockfs_mount *mount, const char *path) {
    struct exfat_sector_transaction tx;
    struct exfat_root_entry *entry;
    char name[VFS_DIRENT_NAME_MAX];
    uint32_t clusters[EXFAT_ALLOC_PLAN_CLUSTER_COUNT];
    uint64_t cluster_count = 0;
    uint32_t first_cluster = 0;

    if (mount == 0 || path == 0 || !mount->exfat.commit_ready ||
        parse_exfat_test_short_name(path, name, sizeof(name)) != 0) {
        return -1;
    }

    entry = find_exfat_root_entry(mount, path);
    if (entry == 0 || !entry->writable || entry->is_dir ||
        !chars_equal(entry->parent, "/")) {
        return -1;
    }

    first_cluster = entry->first_cluster;
    if (collect_exfat_test_cluster_chain_for_test(mount, entry, clusters, &cluster_count) != 0) {
        return -1;
    }

    exfat_sector_transaction_begin_for_test(&tx, mount);
    if (mark_exfat_test_directory_entries_deleted_in_transaction(&tx, entry) != 0 ||
        release_exfat_test_cluster_chain_in_transaction(&tx, clusters, cluster_count) != 0 ||
        exfat_sector_transaction_commit_for_test(&tx) != 0) {
        return -1;
    }

    for (uint64_t i = 0; i < cluster_count; i++) {
        if (zero_exfat_cluster_for_test(mount, clusters[i]) != 0) {
            log_warn("exFAT test unlink cleanup failed: source=%s cluster=%u\n",
                     mount->source, (uint64_t)clusters[i]);
            break;
        }
    }

    log_info("exFAT test file unlinked: source=%s path=/%s first_cluster=%u\n",
             mount->source, name, (uint64_t)first_cluster);
    clear_exfat_root_entry(entry);
    return 0;
}
#endif

static void plan_exfat_create_transaction(struct blockfs_mount *mount) {
    uint8_t sector[512];
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t cluster_size = sectors_per_cluster * sizeof(sector);
    uint64_t first_bit = 0;
    uint64_t first_bitmap_byte = 0;
    uint64_t bitmap_file_cluster_index = 0;
    uint64_t bitmap_offset_in_cluster = 0;
    uint64_t bitmap_sector_index = 0;
    uint64_t fat_byte_offset = 0;
    uint32_t bitmap_cluster = 0;
    uint64_t bitmap_cluster_lba = 0;

    reset_exfat_transaction_plan(mount);
    if (!mount->exfat.create_plan_entries_ready ||
        !mount->exfat.allocation_plan_ready ||
        !mount->exfat.allocation_bitmap_present ||
        mount->block.block_size != sizeof(sector) ||
        sectors_per_cluster == 0 ||
        cluster_size == 0) {
        return;
    }

    first_bit = (uint64_t)mount->exfat.allocation_plan_clusters[0] - 2;
    first_bitmap_byte = first_bit / 8;
    bitmap_file_cluster_index = first_bitmap_byte / cluster_size;
    bitmap_offset_in_cluster = first_bitmap_byte % cluster_size;
    bitmap_sector_index = bitmap_offset_in_cluster / sizeof(sector);

    if (exfat_resolve_cluster(mount, mount->exfat.allocation_bitmap_cluster, 0,
                              bitmap_file_cluster_index, &bitmap_cluster) != 0) {
        return;
    }
    bitmap_cluster_lba = exfat_cluster_lba(mount, bitmap_cluster);
    if (bitmap_cluster_lba == (uint64_t)-1 ||
        block_read(mount->block_index, bitmap_cluster_lba + bitmap_sector_index, 1, sector) != 0) {
        return;
    }
    mount->exfat.transaction_bitmap_lba = bitmap_cluster_lba + bitmap_sector_index;
    mount->exfat.transaction_bitmap_byte_offset = bitmap_offset_in_cluster % sizeof(sector);
    mount->exfat.transaction_bitmap_old_value =
        sector[mount->exfat.transaction_bitmap_byte_offset];
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        uint64_t bit = (uint64_t)mount->exfat.allocation_plan_clusters[i] - 2;
        uint64_t byte_index = bit / 8;
        if (byte_index == first_bitmap_byte) {
            mount->exfat.transaction_bitmap_mask |= (uint8_t)(1U << (bit % 8));
        }
    }
    mount->exfat.transaction_bitmap_new_value =
        mount->exfat.transaction_bitmap_old_value | mount->exfat.transaction_bitmap_mask;

    fat_byte_offset = (uint64_t)mount->exfat.allocation_plan_clusters[0] * 4;
    mount->exfat.transaction_fat_lba =
        (uint64_t)mount->exfat.fat_offset + fat_byte_offset / sizeof(sector);
    if (block_read(mount->block_index, mount->exfat.transaction_fat_lba, 1, sector) != 0) {
        return;
    }
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        uint32_t cluster = mount->exfat.allocation_plan_clusters[i];
        uint64_t entry_byte_offset = (uint64_t)cluster * 4;
        uint64_t entry_lba = (uint64_t)mount->exfat.fat_offset +
                             entry_byte_offset / sizeof(sector);
        uint64_t sector_offset = entry_byte_offset % sizeof(sector);
        if (entry_lba != mount->exfat.transaction_fat_lba ||
            sector_offset + 4 > sizeof(sector)) {
            return;
        }
        mount->exfat.transaction_fat_offsets[i] = sector_offset;
        mount->exfat.transaction_fat_old_values[i] = read_le32(sector + sector_offset);
        if (i + 1 < EXFAT_ALLOC_PLAN_CLUSTER_COUNT) {
            mount->exfat.transaction_fat_new_values[i] = mount->exfat.allocation_plan_clusters[i + 1];
        } else {
            mount->exfat.transaction_fat_new_values[i] = 0xffffffffU;
        }
    }

    mount->exfat.transaction_directory_lba = mount->exfat.create_plan_entry_lba;
    mount->exfat.transaction_directory_byte_offset =
        mount->exfat.create_plan_first_slot * EXFAT_ENTRY_SIZE;
    mount->exfat.transaction_directory_byte_count = EXFAT_CREATE_PLAN_ENTRY_BYTES;
    mount->exfat.transaction_plan_write_count = 3;
    mount->exfat.transaction_plan_ready = 1;

    log_info("exFAT write transaction planned: bitmap lba=%u off=%u %u->%u fat lba=%u dir lba=%u off=%u bytes=%u\n",
             mount->exfat.transaction_bitmap_lba,
             mount->exfat.transaction_bitmap_byte_offset,
             mount->exfat.transaction_bitmap_old_value,
             mount->exfat.transaction_bitmap_new_value,
             mount->exfat.transaction_fat_lba,
             mount->exfat.transaction_directory_lba,
             mount->exfat.transaction_directory_byte_offset,
             mount->exfat.transaction_directory_byte_count);

    dry_run_exfat_create_transaction(mount);
    commit_exfat_create_transaction_for_test(mount);
}

static void reset_exfat_create_plan(struct blockfs_mount *mount) {
    mount->exfat.create_plan_required_entries = EXFAT_CREATE_PLAN_REQUIRED_ENTRIES;
    mount->exfat.create_plan_found_entries = 0;
    mount->exfat.create_plan_directory_cluster = 0;
    mount->exfat.create_plan_directory_cluster_index = 0;
    mount->exfat.create_plan_sector_index = 0;
    mount->exfat.create_plan_first_slot = 0;
    mount->exfat.create_plan_entry_lba = 0;
    mount->exfat.create_plan_ready = 0;
    mount->exfat.create_plan_file_size = 0;
    mount->exfat.create_plan_first_cluster = 0;
    mount->exfat.create_plan_name_hash = 0;
    mount->exfat.create_plan_entry_set_checksum = 0;
    mount->exfat.create_plan_entries_ready = 0;
    zero_bytes(mount->exfat.create_plan_entries, sizeof(mount->exfat.create_plan_entries));
    reset_exfat_transaction_plan(mount);
}

static void plan_exfat_root_directory_create(struct blockfs_mount *mount) {
    uint8_t sector[512];
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t slots_per_sector = sizeof(sector) / EXFAT_ENTRY_SIZE;
    uint64_t run = 0;

    reset_exfat_create_plan(mount);
    if (!exfat_cluster_valid(mount, mount->exfat.root_dir_cluster) ||
        mount->block.block_size != sizeof(sector) ||
        sectors_per_cluster == 0) {
        return;
    }

    for (uint64_t cluster_index = 0;
         cluster_index < mount->exfat.cluster_count;
         cluster_index++) {
        uint32_t cluster = 0;
        uint64_t lba;

        if (exfat_resolve_cluster(mount, mount->exfat.root_dir_cluster, 0,
                                  cluster_index, &cluster) != 0) {
            return;
        }
        lba = exfat_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1) {
            return;
        }

        for (uint64_t sector_index = 0; sector_index < sectors_per_cluster; sector_index++) {
            if (block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
                return;
            }
            for (uint64_t slot = 0; slot < slots_per_sector; slot++) {
                const uint8_t *entry = sector + slot * EXFAT_ENTRY_SIZE;
                if (exfat_directory_slot_free(entry)) {
                    if (run == 0) {
                        mount->exfat.create_plan_directory_cluster = cluster;
                        mount->exfat.create_plan_directory_cluster_index = cluster_index;
                        mount->exfat.create_plan_sector_index = sector_index;
                        mount->exfat.create_plan_first_slot = slot;
                        mount->exfat.create_plan_entry_lba = lba + sector_index;
                    }
                    run++;
                    mount->exfat.create_plan_found_entries = run;
                    if (run >= EXFAT_CREATE_PLAN_REQUIRED_ENTRIES) {
                        mount->exfat.create_plan_ready = 1;
                        log_info("exFAT directory create plan: path=/%s entries=%u cluster=%u sector=%u slot=%u lba=%u\n",
                                 EXFAT_CREATE_PLAN_NAME,
                                 mount->exfat.create_plan_required_entries,
                                 mount->exfat.create_plan_directory_cluster,
                                 mount->exfat.create_plan_sector_index,
                                 mount->exfat.create_plan_first_slot,
                                 mount->exfat.create_plan_entry_lba);
                        return;
                    }
                } else {
                    run = 0;
                    mount->exfat.create_plan_found_entries = 0;
                }
            }
        }
    }
}

static void parse_exfat_directory_chain(struct blockfs_mount *mount, const char *parent,
                                        uint32_t first_cluster, uint8_t stream_flags) {
    uint8_t sector[512];
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t stop = 0;

    if (!exfat_cluster_valid(mount, first_cluster) ||
        mount->block.block_size != sizeof(sector)) {
        return;
    }

    for (uint64_t cluster_index = 0;
         cluster_index < mount->exfat.cluster_count && !stop;
         cluster_index++) {
        uint32_t cluster = 0;
        uint64_t lba;

        if (exfat_resolve_cluster(mount, first_cluster, stream_flags,
                                  cluster_index, &cluster) != 0) {
            return;
        }
        lba = exfat_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1) {
            return;
        }

        for (uint64_t sector_index = 0; sector_index < sectors_per_cluster && !stop; sector_index++) {
            if (block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
                return;
            }
            if (parse_exfat_directory_sector(mount, sector, parent, &stop) != 0) {
                return;
            }
        }
    }
}

static uint64_t count_exfat_bitmap_sector_used(const struct blockfs_mount *mount,
                                               const uint8_t *sector,
                                               uint64_t bytes_to_scan,
                                               uint64_t *cluster_bit_index,
                                               uint32_t *first_free_cluster,
                                               uint32_t *plan_clusters,
                                               uint64_t plan_capacity,
                                               uint64_t *plan_count) {
    uint64_t used = 0;

    for (uint64_t byte_index = 0;
         byte_index < bytes_to_scan && *cluster_bit_index < mount->exfat.cluster_count;
         byte_index++) {
        uint8_t value = sector[byte_index];
        for (uint64_t bit = 0;
             bit < 8 && *cluster_bit_index < mount->exfat.cluster_count;
             bit++) {
            if ((value & (uint8_t)(1U << bit)) != 0) {
                used++;
            } else if (*first_free_cluster == 0) {
                *first_free_cluster = (uint32_t)(*cluster_bit_index + 2);
            }
            if ((value & (uint8_t)(1U << bit)) == 0 &&
                plan_clusters != 0 &&
                plan_count != 0 &&
                *plan_count < plan_capacity) {
                plan_clusters[*plan_count] = (uint32_t)(*cluster_bit_index + 2);
                (*plan_count)++;
            }
            (*cluster_bit_index)++;
        }
    }

    return used;
}

static void parse_exfat_allocation_bitmap(struct blockfs_mount *mount) {
    uint8_t sector[512];
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t bytes_remaining = mount->exfat.allocation_bitmap_size;
    uint64_t cluster_bit_index = 0;
    uint64_t used = 0;
    uint32_t first_free_cluster = 0;
    uint64_t plan_count = 0;

    mount->exfat.allocation_bitmap_scanned_clusters = 0;
    mount->exfat.allocation_bitmap_used_clusters = 0;
    mount->exfat.allocation_bitmap_free_clusters = 0;
    mount->exfat.allocation_bitmap_first_free_cluster = 0;
    mount->exfat.allocation_bitmap_complete = 0;
    mount->exfat.allocation_plan_requested_clusters = EXFAT_ALLOC_PLAN_CLUSTER_COUNT;
    mount->exfat.allocation_plan_found_clusters = 0;
    mount->exfat.allocation_plan_ready = 0;
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        mount->exfat.allocation_plan_clusters[i] = 0;
    }
    if (!mount->exfat.allocation_bitmap_present ||
        !exfat_cluster_valid(mount, mount->exfat.allocation_bitmap_cluster) ||
        mount->block.block_size != sizeof(sector) ||
        sectors_per_cluster == 0) {
        return;
    }

    for (uint64_t cluster_index = 0;
         bytes_remaining > 0 && cluster_bit_index < mount->exfat.cluster_count;
         cluster_index++) {
        uint32_t cluster = 0;
        uint64_t lba;

        if (exfat_resolve_cluster(mount, mount->exfat.allocation_bitmap_cluster, 0,
                                  cluster_index, &cluster) != 0) {
            return;
        }
        lba = exfat_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1) {
            return;
        }

        for (uint64_t sector_index = 0;
             sector_index < sectors_per_cluster &&
             bytes_remaining > 0 &&
             cluster_bit_index < mount->exfat.cluster_count;
             sector_index++) {
            uint64_t bytes_to_scan = sizeof(sector);
            if (bytes_to_scan > bytes_remaining) {
                bytes_to_scan = bytes_remaining;
            }
            if (block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
                return;
            }
            used += count_exfat_bitmap_sector_used(mount, sector, bytes_to_scan,
                                                   &cluster_bit_index,
                                                   &first_free_cluster,
                                                   mount->exfat.allocation_plan_clusters,
                                                   EXFAT_ALLOC_PLAN_CLUSTER_COUNT,
                                                   &plan_count);
            bytes_remaining -= bytes_to_scan;
        }
    }

    mount->exfat.allocation_bitmap_scanned_clusters = cluster_bit_index;
    mount->exfat.allocation_bitmap_used_clusters = used;
    if (cluster_bit_index >= mount->exfat.cluster_count) {
        mount->exfat.allocation_bitmap_complete = 1;
        mount->exfat.allocation_bitmap_free_clusters = mount->exfat.cluster_count - used;
    } else if (cluster_bit_index >= used) {
        mount->exfat.allocation_bitmap_free_clusters = cluster_bit_index - used;
    }
    mount->exfat.allocation_bitmap_first_free_cluster = first_free_cluster;
    mount->exfat.allocation_plan_found_clusters = plan_count;
    mount->exfat.allocation_plan_ready = plan_count >= EXFAT_ALLOC_PLAN_CLUSTER_COUNT;

    log_info("exFAT allocation bitmap parsed: scanned=%u used=%u free=%u first_free=%u complete=%u\n",
             mount->exfat.allocation_bitmap_scanned_clusters,
             mount->exfat.allocation_bitmap_used_clusters,
             mount->exfat.allocation_bitmap_free_clusters,
             mount->exfat.allocation_bitmap_first_free_cluster,
             mount->exfat.allocation_bitmap_complete);
    if (mount->exfat.allocation_plan_ready) {
        log_info("exFAT allocation plan: requested=%u found=%u clusters=%u,%u,%u\n",
                 mount->exfat.allocation_plan_requested_clusters,
                 mount->exfat.allocation_plan_found_clusters,
                 mount->exfat.allocation_plan_clusters[0],
                 mount->exfat.allocation_plan_clusters[1],
                 mount->exfat.allocation_plan_clusters[2]);
    }
}

static void append_exfat_allocation_plan(struct blockfs_mount *mount,
                                         char *buffer,
                                         uint64_t buffer_len,
                                         uint64_t *pos) {
    append_string(buffer, buffer_len, pos, "\nallocation_plan_requested_clusters: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_requested_clusters);
    append_string(buffer, buffer_len, pos, "\nallocation_plan_found_clusters: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_found_clusters);
    append_string(buffer, buffer_len, pos, "\nallocation_plan_ready: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_ready);
    append_string(buffer, buffer_len, pos, "\nallocation_plan_clusters: ");
    for (uint64_t i = 0; i < mount->exfat.allocation_plan_found_clusters &&
                         i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        if (i > 0) {
            append_char(buffer, buffer_len, pos, ' ');
        }
        append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_clusters[i]);
    }
    append_string(buffer, buffer_len, pos, "\nallocation_plan_fat_chain: ");
    if (!mount->exfat.allocation_plan_ready) {
        append_string(buffer, buffer_len, pos, "unavailable");
        return;
    }
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        if (i > 0) {
            append_string(buffer, buffer_len, pos, "->");
        }
        append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_clusters[i]);
    }
    append_string(buffer, buffer_len, pos, "->EOF");
}

static void append_exfat_create_plan(struct blockfs_mount *mount,
                                     char *buffer,
                                     uint64_t buffer_len,
                                     uint64_t *pos) {
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_path: /");
    append_string(buffer, buffer_len, pos, EXFAT_CREATE_PLAN_NAME);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_name_chars: ");
    append_u64(buffer, buffer_len, pos, EXFAT_CREATE_PLAN_NAME_CHARS);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_required_entries: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_required_entries);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_found_entries: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_found_entries);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_ready: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_ready);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_cluster: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_directory_cluster);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_cluster_index: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_directory_cluster_index);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_sector_index: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_sector_index);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_first_slot: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_first_slot);
    append_string(buffer, buffer_len, pos, "\ndirectory_plan_entry_lba: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_entry_lba);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry_plan_ready: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_entries_ready);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry_plan_first_cluster: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_first_cluster);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry_plan_file_size: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_file_size);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry_plan_name_hash: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_name_hash);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry_plan_checksum: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.create_plan_entry_set_checksum);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry0_hex: ");
    append_hex_bytes(buffer, buffer_len, pos, mount->exfat.create_plan_entries, EXFAT_ENTRY_SIZE);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry1_hex: ");
    append_hex_bytes(buffer, buffer_len, pos,
                     mount->exfat.create_plan_entries + EXFAT_ENTRY_SIZE,
                     EXFAT_ENTRY_SIZE);
    append_string(buffer, buffer_len, pos, "\ndirectory_entry2_hex: ");
    append_hex_bytes(buffer, buffer_len, pos,
                     mount->exfat.create_plan_entries + EXFAT_ENTRY_SIZE * 2,
                     EXFAT_ENTRY_SIZE);
}

static void append_exfat_transaction_plan(struct blockfs_mount *mount,
                                          char *buffer,
                                          uint64_t buffer_len,
                                          uint64_t *pos) {
    append_string(buffer, buffer_len, pos, "\ntransaction_plan_ready: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_plan_ready);
    append_string(buffer, buffer_len, pos, "\ntransaction_plan_write_count: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_plan_write_count);
    append_string(buffer, buffer_len, pos, "\ntransaction_bitmap_lba: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_bitmap_lba);
    append_string(buffer, buffer_len, pos, "\ntransaction_bitmap_byte_offset: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_bitmap_byte_offset);
    append_string(buffer, buffer_len, pos, "\ntransaction_bitmap_old_hex: ");
    append_hex_byte(buffer, buffer_len, pos, mount->exfat.transaction_bitmap_old_value);
    append_string(buffer, buffer_len, pos, "\ntransaction_bitmap_new_hex: ");
    append_hex_byte(buffer, buffer_len, pos, mount->exfat.transaction_bitmap_new_value);
    append_string(buffer, buffer_len, pos, "\ntransaction_bitmap_mask_hex: ");
    append_hex_byte(buffer, buffer_len, pos, mount->exfat.transaction_bitmap_mask);
    append_string(buffer, buffer_len, pos, "\ntransaction_fat_lba: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_fat_lba);
    append_string(buffer, buffer_len, pos, "\ntransaction_fat_plan: ");
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        if (i > 0) {
            append_char(buffer, buffer_len, pos, ' ');
        }
        append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_clusters[i]);
        append_char(buffer, buffer_len, pos, '@');
        append_u64(buffer, buffer_len, pos, mount->exfat.transaction_fat_offsets[i]);
        append_char(buffer, buffer_len, pos, ':');
        append_u64(buffer, buffer_len, pos, mount->exfat.transaction_fat_old_values[i]);
        append_string(buffer, buffer_len, pos, "->");
        if (mount->exfat.transaction_fat_new_values[i] == 0xffffffffU) {
            append_string(buffer, buffer_len, pos, "EOF");
        } else {
            append_u64(buffer, buffer_len, pos, mount->exfat.transaction_fat_new_values[i]);
        }
    }
    append_string(buffer, buffer_len, pos, "\ntransaction_directory_lba: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_directory_lba);
    append_string(buffer, buffer_len, pos, "\ntransaction_directory_byte_offset: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_directory_byte_offset);
    append_string(buffer, buffer_len, pos, "\ntransaction_directory_byte_count: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.transaction_directory_byte_count);
    append_string(buffer, buffer_len, pos, "\npatched_transaction_ready: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.patched_transaction_ready);
    append_string(buffer, buffer_len, pos, "\npatched_bitmap_value_hex: ");
    append_hex_byte(buffer, buffer_len, pos, mount->exfat.patched_bitmap_value);
    append_string(buffer, buffer_len, pos, "\npatched_fat_chain: ");
    for (uint64_t i = 0; i < EXFAT_ALLOC_PLAN_CLUSTER_COUNT; i++) {
        if (i > 0) {
            append_string(buffer, buffer_len, pos, "->");
        }
        append_u64(buffer, buffer_len, pos, mount->exfat.allocation_plan_clusters[i]);
    }
    append_string(buffer, buffer_len, pos, "->EOF");
    append_string(buffer, buffer_len, pos, "\npatched_directory_checksum: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.patched_directory_checksum);
    append_string(buffer, buffer_len, pos, "\npatched_directory_first_cluster: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.patched_directory_first_cluster);
    append_string(buffer, buffer_len, pos, "\npatched_directory_file_size: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.patched_directory_file_size);
    append_string(buffer, buffer_len, pos, "\npatched_directory_name_length: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.patched_directory_name_length);
    append_string(buffer, buffer_len, pos, "\npatched_directory_name_matches: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.patched_directory_name_matches);
    append_string(buffer, buffer_len, pos, "\ncommit_supported: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.commit_supported);
    append_string(buffer, buffer_len, pos, "\ncommit_attempted: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.commit_attempted);
    append_string(buffer, buffer_len, pos, "\ncommit_ready: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.commit_ready);
    append_string(buffer, buffer_len, pos, "\ncommit_write_count: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.commit_write_count);
    append_string(buffer, buffer_len, pos, "\ncommit_verified: ");
    append_u64(buffer, buffer_len, pos, mount->exfat.commit_verified);
}

static void parse_exfat_root_directory(struct blockfs_mount *mount) {
    reset_exfat_root_entries(mount);
    parse_exfat_directory_chain(mount, "/", mount->exfat.root_dir_cluster, 0);

    for (uint64_t i = 0; i < mount->root_entry_count; i++) {
        char child_path[VFS_MOUNT_PATH_MAX];
        struct exfat_root_entry *entry = &mount->root_entries[i];

        if (!entry->is_dir || !exfat_cluster_valid(mount, entry->first_cluster) ||
            join_child_path(child_path, sizeof(child_path), entry->parent, entry->name) != 0) {
            continue;
        }
        parse_exfat_directory_chain(mount, child_path, entry->first_cluster, entry->stream_flags);
    }

    parse_exfat_allocation_bitmap(mount);
    plan_exfat_root_directory_create(mount);
    encode_exfat_create_plan_entries(mount);
    plan_exfat_create_transaction(mount);
}

static int fat32_geometry_valid(const struct blockfs_mount *mount) {
    uint64_t data_start;

    if (!mount->fat32.present ||
        mount->fat32.bytes_per_sector != mount->block.block_size ||
        mount->fat32.sectors_per_cluster == 0 ||
        mount->fat32.reserved_sectors == 0 ||
        mount->fat32.number_of_fats == 0 ||
        mount->fat32.fat_size_sectors == 0 ||
        mount->fat32.root_dir_cluster < 2) {
        return 0;
    }

    data_start = (uint64_t)mount->fat32.reserved_sectors +
                 (uint64_t)mount->fat32.number_of_fats * mount->fat32.fat_size_sectors;
    return data_start < mount->fat32.total_sectors;
}

static uint64_t fat32_data_start_lba(const struct blockfs_mount *mount) {
    return (uint64_t)mount->fat32.reserved_sectors +
           (uint64_t)mount->fat32.number_of_fats * mount->fat32.fat_size_sectors;
}

static uint64_t fat32_cluster_count(const struct blockfs_mount *mount) {
    uint64_t data_start;

    if (!fat32_geometry_valid(mount)) {
        return 0;
    }
    data_start = fat32_data_start_lba(mount);
    return (mount->fat32.total_sectors - data_start) / mount->fat32.sectors_per_cluster;
}

static int fat32_cluster_valid(const struct blockfs_mount *mount, uint32_t cluster) {
    uint64_t cluster_count = fat32_cluster_count(mount);

    return cluster >= 2 && cluster < cluster_count + 2;
}

static uint64_t fat32_cluster_lba(const struct blockfs_mount *mount, uint32_t cluster) {
    if (!fat32_cluster_valid(mount, cluster)) {
        return (uint64_t)-1;
    }
    return fat32_data_start_lba(mount) +
           ((uint64_t)cluster - 2) * mount->fat32.sectors_per_cluster;
}

static int fat32_read_fat_entry(struct blockfs_mount *mount, uint32_t cluster, uint32_t *next) {
    uint8_t sector[512];
    uint64_t fat_byte_offset;
    uint64_t fat_sector;
    uint64_t sector_offset;

    if (!fat32_cluster_valid(mount, cluster) || next == 0) {
        return -1;
    }

    fat_byte_offset = (uint64_t)cluster * 4;
    fat_sector = (uint64_t)mount->fat32.reserved_sectors + fat_byte_offset / sizeof(sector);
    sector_offset = fat_byte_offset % sizeof(sector);
    if (fat_sector >= (uint64_t)mount->fat32.reserved_sectors + mount->fat32.fat_size_sectors ||
        sector_offset + 4 > sizeof(sector) ||
        block_read(mount->block_index, fat_sector, 1, sector) != 0) {
        return -1;
    }

    *next = read_le32(sector + sector_offset) & 0x0fffffffU;
    return 0;
}

static int fat32_resolve_cluster(struct blockfs_mount *mount, uint32_t first_cluster,
                                 uint64_t cluster_index, uint32_t *cluster_out) {
    uint32_t cluster = first_cluster;

    if (!fat32_cluster_valid(mount, cluster) || cluster_out == 0) {
        return -1;
    }
    for (uint64_t i = 0; i < cluster_index; i++) {
        uint32_t next = 0;
        if (fat32_read_fat_entry(mount, cluster, &next) != 0 ||
            next >= 0x0ffffff8U ||
            !fat32_cluster_valid(mount, next)) {
            return -1;
        }
        cluster = next;
    }

    *cluster_out = cluster;
    return 0;
}

static void fat32_short_name_to_text(const uint8_t *entry, char *out, uint64_t out_len) {
    uint64_t pos = 0;
    uint64_t name_len = 8;
    uint64_t ext_len = 3;

    if (out_len == 0) {
        return;
    }
    while (name_len > 0 && entry[name_len - 1] == ' ') {
        name_len--;
    }
    while (ext_len > 0 && entry[8 + ext_len - 1] == ' ') {
        ext_len--;
    }
    for (uint64_t i = 0; i < name_len && pos + 1 < out_len; i++) {
        out[pos++] = (char)entry[i];
    }
    if (ext_len > 0 && pos + 1 < out_len) {
        out[pos++] = '.';
    }
    for (uint64_t i = 0; i < ext_len && pos + 1 < out_len; i++) {
        out[pos++] = (char)entry[8 + i];
    }
    out[pos] = '\0';
}

static uint8_t fat32_short_name_checksum(const uint8_t *short_name) {
    uint8_t sum = 0;

    for (uint64_t i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + short_name[i]);
    }
    return sum;
}

static void fat32_lfn_reset(struct fat32_lfn_state *state) {
    if (state == 0) {
        return;
    }
    state->name[0] = '\0';
    state->checksum = 0;
    state->total_entries = 0;
    state->seen_entries = 0;
    state->present = 0;
    state->valid = 0;
}

static void fat32_lfn_decode_entry(const uint8_t *entry, struct fat32_lfn_state *state) {
    static const uint8_t offsets[13] = {
        1, 3, 5, 7, 9,
        14, 16, 18, 20, 22, 24,
        28, 30
    };
    uint8_t order;
    uint8_t sequence;
    uint64_t base;

    if (state == 0) {
        return;
    }
    order = entry[0] & 0x1f;
    sequence = entry[0];
    if (order == 0 || order > 5) {
        fat32_lfn_reset(state);
        return;
    }
    if ((sequence & 0x40U) != 0) {
        fat32_lfn_reset(state);
        state->total_entries = order;
        state->checksum = entry[13];
        state->valid = 1;
    } else if (!state->valid || order > state->total_entries || entry[13] != state->checksum) {
        fat32_lfn_reset(state);
        return;
    }

    base = ((uint64_t)order - 1) * 13;
    for (uint64_t i = 0; i < 13; i++) {
        uint16_t ch = read_le16(entry + offsets[i]);
        uint64_t out_index = base + i;

        if (ch == 0x0000) {
            if (out_index < sizeof(state->name)) {
                state->name[out_index] = '\0';
            }
            break;
        }
        if (ch == 0xffff) {
            break;
        }
        if (out_index + 1 >= sizeof(state->name)) {
            continue;
        }
        state->name[out_index] = (ch < 0x80) ? (char)ch : '?';
    }
    state->seen_entries++;
    state->present = 1;
}

static int fat32_entry_valid(const uint8_t *entry) {
    uint8_t first = entry[0];
    uint8_t attr = entry[11];

    return first != 0x00 &&
           first != 0xe5 &&
           first != 0x05 &&
           first != '.' &&
           attr != 0x0f &&
           (attr & 0x08) == 0;
}

static void parse_fat32_directory_cluster(struct blockfs_mount *mount,
                                          const char *parent,
                                          uint32_t cluster) {
    uint8_t sector[512];
    struct fat32_lfn_state lfn;
    uint64_t lba = fat32_cluster_lba(mount, cluster);

    fat32_lfn_reset(&lfn);
    if (lba == (uint64_t)-1) {
        return;
    }
    for (uint64_t sector_index = 0; sector_index < mount->fat32.sectors_per_cluster; sector_index++) {
        if (block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
            return;
        }
        for (uint64_t off = 0; off < sizeof(sector); off += 32) {
            const uint8_t *entry = sector + off;
            char name[VFS_DIRENT_NAME_MAX];
            uint8_t attr = entry[11];
            uint32_t first_cluster;
            uint64_t size;

            if (entry[0] == 0x00) {
                return;
            }
            if (entry[0] == 0xe5) {
                fat32_lfn_reset(&lfn);
                continue;
            }
            if (attr == 0x0f) {
                fat32_lfn_decode_entry(entry, &lfn);
                continue;
            }
            if (!fat32_entry_valid(entry)) {
                fat32_lfn_reset(&lfn);
                continue;
            }
            first_cluster = ((uint32_t)read_le16(entry + 20) << 16) |
                            (uint32_t)read_le16(entry + 26);
            size = read_le32(entry + 28);
            if (lfn.valid && lfn.present && lfn.name[0] != '\0' &&
                lfn.seen_entries == lfn.total_entries &&
                lfn.checksum == fat32_short_name_checksum(entry)) {
                copy_limited(name, sizeof(name), lfn.name);
            } else {
                fat32_short_name_to_text(entry, name, sizeof(name));
            }
            add_exfat_root_entry(mount, parent, name, size, first_cluster, attr, 0);
            fat32_lfn_reset(&lfn);
        }
    }
}

static void parse_fat32_directory_chain(struct blockfs_mount *mount,
                                        const char *parent,
                                        uint32_t first_cluster) {
    uint32_t cluster = first_cluster;

    for (uint64_t guard = 0; guard < fat32_cluster_count(mount); guard++) {
        uint32_t next = 0;

        parse_fat32_directory_cluster(mount, parent, cluster);
        if (fat32_read_fat_entry(mount, cluster, &next) != 0 || next >= 0x0ffffff8U) {
            break;
        }
        if (!fat32_cluster_valid(mount, next)) {
            break;
        }
        cluster = next;
    }
}

static void parse_fat32_root_directory(struct blockfs_mount *mount) {
    reset_exfat_root_entries(mount);
    parse_fat32_directory_chain(mount, "/", mount->fat32.root_dir_cluster);

    for (uint64_t i = 0; i < mount->root_entry_count; i++) {
        char child_path[VFS_MOUNT_PATH_MAX];
        struct exfat_root_entry *entry = &mount->root_entries[i];

        if (!entry->is_dir || !fat32_cluster_valid(mount, entry->first_cluster) ||
            join_child_path(child_path, sizeof(child_path), entry->parent, entry->name) != 0) {
            continue;
        }
        parse_fat32_directory_chain(mount, child_path, entry->first_cluster);
    }
}

static uint64_t build_info(struct blockfs_mount *mount, char *buffer, uint64_t buffer_len) {
    uint64_t pos = 0;

    if (buffer_len == 0) {
        return 0;
    }
    buffer[0] = '\0';
    append_string(buffer, buffer_len, &pos, "TangPingOS block-backed mount\n");
    append_string(buffer, buffer_len, &pos, "device: ");
    append_string(buffer, buffer_len, &pos, mount->block.name);
    append_string(buffer, buffer_len, &pos, "\nblock_size: ");
    append_u64(buffer, buffer_len, &pos, mount->block.block_size);
    append_string(buffer, buffer_len, &pos, "\nblocks: ");
    append_u64(buffer, buffer_len, &pos, mount->block.block_count);
    append_string(buffer, buffer_len, &pos, "\nwritable: ");
    append_u64(buffer, buffer_len, &pos, mount->block.writable);
    append_string(buffer, buffer_len, &pos, "\n");

    if (mount->exfat.present) {
        append_string(buffer, buffer_len, &pos, "detected_fs: exfat\n");
        append_string(buffer, buffer_len, &pos, "bytes_per_sector: ");
        append_u64(buffer, buffer_len, &pos, one_shifted_by(mount->exfat.bytes_per_sector_shift));
        append_string(buffer, buffer_len, &pos, "\nsectors_per_cluster: ");
        append_u64(buffer, buffer_len, &pos, one_shifted_by(mount->exfat.sectors_per_cluster_shift));
        append_string(buffer, buffer_len, &pos, "\npartition_offset: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.partition_offset);
        append_string(buffer, buffer_len, &pos, "\nvolume_length: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.volume_length);
        append_string(buffer, buffer_len, &pos, "\nfat_offset: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.fat_offset);
        append_string(buffer, buffer_len, &pos, "\nfat_length: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.fat_length);
        append_string(buffer, buffer_len, &pos, "\ncluster_heap_offset: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.cluster_heap_offset);
        append_string(buffer, buffer_len, &pos, "\ncluster_count: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.cluster_count);
        append_string(buffer, buffer_len, &pos, "\nroot_dir_cluster: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.root_dir_cluster);
        append_string(buffer, buffer_len, &pos, "\nnumber_of_fats: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.number_of_fats);
        append_string(buffer, buffer_len, &pos, "\npercent_in_use: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.percent_in_use);
        append_string(buffer, buffer_len, &pos, "\nallocation_bitmap: ");
        append_string(buffer, buffer_len, &pos,
                      mount->exfat.allocation_bitmap_present ? "present" : "missing");
        append_string(buffer, buffer_len, &pos, "\nbitmap_cluster: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_cluster);
        append_string(buffer, buffer_len, &pos, "\nbitmap_size: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_size);
        append_string(buffer, buffer_len, &pos, "\nbitmap_scanned_clusters: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_scanned_clusters);
        append_string(buffer, buffer_len, &pos, "\nbitmap_used_clusters: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_used_clusters);
        append_string(buffer, buffer_len, &pos, "\nbitmap_free_clusters: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_free_clusters);
        append_string(buffer, buffer_len, &pos, "\nbitmap_first_free_cluster: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_first_free_cluster);
        append_string(buffer, buffer_len, &pos, "\nbitmap_complete: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.allocation_bitmap_complete);
        append_exfat_allocation_plan(mount, buffer, buffer_len, &pos);
        append_exfat_create_plan(mount, buffer, buffer_len, &pos);
        append_exfat_transaction_plan(mount, buffer, buffer_len, &pos);
        append_string(buffer, buffer_len, &pos, "\nupcase_table: ");
        append_string(buffer, buffer_len, &pos,
                      mount->exfat.upcase_table_present ? "present" : "missing");
        append_string(buffer, buffer_len, &pos, "\nupcase_cluster: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.upcase_table_cluster);
        append_string(buffer, buffer_len, &pos, "\nupcase_size: ");
        append_u64(buffer, buffer_len, &pos, mount->exfat.upcase_table_size);
        append_string(buffer, buffer_len, &pos, "\nroot_entries: ");
        append_u64(buffer, buffer_len, &pos, mount->root_entry_count);
        append_string(buffer, buffer_len, &pos, "\n");
    } else if (mount->fat32.present) {
        append_string(buffer, buffer_len, &pos, "detected_fs: fat32\nbytes_per_sector: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.bytes_per_sector);
        append_string(buffer, buffer_len, &pos, "\nsectors_per_cluster: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.sectors_per_cluster);
        append_string(buffer, buffer_len, &pos, "\nreserved_sectors: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.reserved_sectors);
        append_string(buffer, buffer_len, &pos, "\nnumber_of_fats: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.number_of_fats);
        append_string(buffer, buffer_len, &pos, "\ntotal_sectors: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.total_sectors);
        append_string(buffer, buffer_len, &pos, "\nfat_size_sectors: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.fat_size_sectors);
        append_string(buffer, buffer_len, &pos, "\nroot_dir_cluster: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.root_dir_cluster);
        append_string(buffer, buffer_len, &pos, "\nfsinfo_sector: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.fsinfo_sector);
        append_string(buffer, buffer_len, &pos, "\nbackup_boot_sector: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.backup_boot_sector);
        append_string(buffer, buffer_len, &pos, "\nhidden_sectors: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.hidden_sectors);
        append_string(buffer, buffer_len, &pos, "\nvolume_id: ");
        append_u64(buffer, buffer_len, &pos, mount->fat32.volume_id);
        append_string(buffer, buffer_len, &pos, "\nvolume_label: ");
        append_string(buffer, buffer_len, &pos, mount->fat32.volume_label);
        append_string(buffer, buffer_len, &pos, "\nroot_entries: ");
        append_u64(buffer, buffer_len, &pos, mount->root_entry_count);
        append_string(buffer, buffer_len, &pos, "\n");
    } else {
        append_string(buffer, buffer_len, &pos, "detected_fs: unknown\n");
    }

    return pos;
}

static uint64_t read_from_buffer(uint64_t offset, void *buffer, uint64_t buffer_len,
                                 const void *data, uint64_t data_len) {
    if (offset >= data_len) {
        return 0;
    }

    uint64_t available = data_len - offset;
    uint64_t to_copy = buffer_len < available ? buffer_len : available;
    copy_bytes(buffer, (const uint8_t *)data + offset, to_copy);
    return to_copy;
}

static struct exfat_root_entry *find_exfat_root_entry(struct blockfs_mount *mount, const char *path) {
    char parent[VFS_MOUNT_PATH_MAX];
    char name[VFS_DIRENT_NAME_MAX];
    uint64_t slash = 0;

    if (path == 0 || path[0] != '/') {
        return 0;
    }
    for (uint64_t i = 1; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }
    if (slash == 0) {
        copy_limited(parent, sizeof(parent), "/");
        copy_limited(name, sizeof(name), path + 1);
    } else {
        if (slash + 1 >= sizeof(parent)) {
            return 0;
        }
        for (uint64_t i = 0; i < slash; i++) {
            parent[i] = path[i];
        }
        parent[slash] = '\0';
        copy_limited(name, sizeof(name), path + slash + 1);
    }
    if (name[0] == '\0') {
        return 0;
    }

    for (uint64_t i = 0; i < mount->root_entry_count; i++) {
        if (chars_equal(parent, mount->root_entries[i].parent) &&
            chars_equal(name, mount->root_entries[i].name)) {
            return &mount->root_entries[i];
        }
    }
    return 0;
}

static int exfat_resolve_file_cluster(struct blockfs_mount *mount, struct exfat_root_entry *entry,
                                      uint64_t file_cluster_index, uint32_t *cluster_out) {
    return exfat_resolve_cluster(mount, entry->first_cluster, entry->stream_flags,
                                 file_cluster_index, cluster_out);
}

static uint64_t read_exfat_root_file(struct blockfs_mount *mount, struct exfat_root_entry *entry,
                                     uint64_t offset, void *buffer, uint64_t buffer_len) {
    uint8_t sector[512];
    uint64_t copied = 0;
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t cluster_size = sectors_per_cluster * sizeof(sector);

    if (entry->is_dir || !exfat_cluster_valid(mount, entry->first_cluster) ||
        buffer == 0 || cluster_size == 0) {
        return (uint64_t)-1;
    }
    if (offset >= entry->size || buffer_len == 0) {
        return 0;
    }

    while (copied < buffer_len && offset + copied < entry->size) {
        uint64_t file_pos = offset + copied;
        uint64_t file_cluster_index = file_pos / cluster_size;
        uint64_t offset_in_cluster = file_pos % cluster_size;
        uint64_t sector_index = offset_in_cluster / sizeof(sector);
        uint64_t sector_offset = offset_in_cluster % sizeof(sector);
        uint64_t available = sizeof(sector) - sector_offset;
        uint64_t remaining = entry->size - file_pos;
        uint64_t to_copy = buffer_len - copied;
        uint32_t cluster = 0;
        uint64_t lba;

        if (exfat_resolve_file_cluster(mount, entry, file_cluster_index, &cluster) != 0) {
            return (uint64_t)-1;
        }
        lba = exfat_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1 ||
            sector_index >= sectors_per_cluster ||
            block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
            return (uint64_t)-1;
        }
        if (to_copy > available) {
            to_copy = available;
        }
        if (to_copy > remaining) {
            to_copy = remaining;
        }

        copy_bytes((uint8_t *)buffer + copied, sector + sector_offset, to_copy);
        copied += to_copy;
    }

    return copied;
}

static uint64_t read_fat32_file(struct blockfs_mount *mount, struct exfat_root_entry *entry,
                                uint64_t offset, void *buffer, uint64_t buffer_len) {
    uint8_t sector[512];
    uint64_t copied = 0;
    uint64_t cluster_size = (uint64_t)mount->fat32.sectors_per_cluster * sizeof(sector);

    if (entry->is_dir || !fat32_cluster_valid(mount, entry->first_cluster) ||
        cluster_size == 0 || offset >= entry->size) {
        return 0;
    }
    if (buffer_len > entry->size - offset) {
        buffer_len = entry->size - offset;
    }

    while (copied < buffer_len) {
        uint64_t file_pos = offset + copied;
        uint64_t file_cluster_index = file_pos / cluster_size;
        uint64_t offset_in_cluster = file_pos % cluster_size;
        uint64_t sector_index = offset_in_cluster / sizeof(sector);
        uint64_t sector_offset = offset_in_cluster % sizeof(sector);
        uint64_t to_copy = buffer_len - copied;
        uint64_t available = sizeof(sector) - sector_offset;
        uint32_t cluster = 0;
        uint64_t lba;

        if (to_copy > available) {
            to_copy = available;
        }
        if (fat32_resolve_cluster(mount, entry->first_cluster, file_cluster_index, &cluster) != 0) {
            return copied;
        }
        lba = fat32_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1 || sector_index >= mount->fat32.sectors_per_cluster ||
            block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
            return copied;
        }
        copy_bytes((uint8_t *)buffer + copied, sector + sector_offset, to_copy);
        copied += to_copy;
    }

    return copied;
}

static uint64_t blockfs_read(const char *path, uint64_t offset,
                             void *buffer, uint64_t buffer_len, void *context) {
    struct blockfs_mount *mount = context;
    struct exfat_root_entry *entry;
    char info[BLOCKFS_INFO_MAX];
    uint8_t sector[512];

    if (mount == 0 || buffer == 0) {
        return (uint64_t)-1;
    }

    if (chars_equal(path, "/info.txt")) {
        uint64_t info_len = build_info(mount, info, sizeof(info));
        return read_from_buffer(offset, buffer, buffer_len, info, info_len);
    }

    if (chars_equal(path, "/sector0.bin")) {
        if (mount->block.block_size != sizeof(sector) ||
            block_read(mount->block_index, 0, 1, sector) != 0) {
            return (uint64_t)-1;
        }
        return read_from_buffer(offset, buffer, buffer_len, sector, sizeof(sector));
    }

    entry = find_exfat_root_entry(mount, path);
    if (entry != 0) {
        if (mount->fat32.present) {
            return read_fat32_file(mount, entry, offset, buffer, buffer_len);
        }
        return read_exfat_root_file(mount, entry, offset, buffer, buffer_len);
    }

    return (uint64_t)-1;
}

static int blockfs_list(const char *path, uint64_t index, struct vfs_dirent *out, void *context) {
    struct blockfs_mount *mount = context;
    uint64_t local_index = 0;

    if (mount == 0 || out == 0) {
        return -1;
    }
    if (chars_equal(path, "/")) {
        if (index == 0) {
            copy_limited(out->name, sizeof(out->name), "info.txt");
            out->type = VFS_DIRENT_TYPE_FILE;
            out->size = 0;
            return 1;
        }
        if (index == 1) {
            copy_limited(out->name, sizeof(out->name), "sector0.bin");
            out->type = VFS_DIRENT_TYPE_FILE;
            out->size = 512;
            return 1;
        }
        index -= 2;
    }

    for (uint64_t i = 0; i < mount->root_entry_count; i++) {
        struct exfat_root_entry *entry = &mount->root_entries[i];
        if (!chars_equal(entry->parent, path)) {
            continue;
        }
        if (local_index != index) {
            local_index++;
            continue;
        }
        copy_limited(out->name, sizeof(out->name), entry->name);
        out->type = entry->is_dir ? VFS_DIRENT_TYPE_DIR : VFS_DIRENT_TYPE_FILE;
        out->size = entry->size;
        return 1;
    }
    return 0;
}

static uint64_t blockfs_size(const char *path, void *context) {
    struct blockfs_mount *mount = context;
    char info[BLOCKFS_INFO_MAX];

    if (mount == 0) {
        return (uint64_t)-1;
    }
    if (chars_equal(path, "/info.txt")) {
        return build_info(mount, info, sizeof(info));
    }
    if (chars_equal(path, "/sector0.bin")) {
        return 512;
    }
    struct exfat_root_entry *entry = find_exfat_root_entry(mount, path);
    if (entry != 0) {
        return entry->size;
    }
    return (uint64_t)-1;
}

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
static uint64_t blockfs_write_new_file_for_test(struct blockfs_mount *mount,
                                                struct exfat_root_entry *entry,
                                                uint64_t offset,
                                                const void *buffer,
                                                uint64_t buffer_len) {
    uint8_t sector[512];
    uint64_t written = 0;
    uint64_t sectors_per_cluster = one_shifted_by(mount->exfat.sectors_per_cluster_shift);
    uint64_t cluster_size = sectors_per_cluster * sizeof(sector);
    uint64_t end_offset;
    uint64_t required_clusters;

    if (mount == 0 || entry == 0 || buffer == 0 || !mount->exfat.commit_ready ||
        !entry->writable ||
        !chars_equal(entry->parent, "/") ||
        cluster_size == 0 ||
        offset > UINT64_MAX - buffer_len) {
        return (uint64_t)-1;
    }

    end_offset = offset + buffer_len;
    if (end_offset > entry->capacity) {
        required_clusters = exfat_test_cluster_count_for_size(mount, end_offset);
        if (required_clusters == 0 ||
            extend_exfat_test_file_for_test(mount, entry, required_clusters) != 0 ||
            end_offset > entry->capacity) {
            return (uint64_t)-1;
        }
    }

    while (written < buffer_len) {
        uint64_t file_pos = offset + written;
        uint64_t file_cluster_index = file_pos / cluster_size;
        uint64_t offset_in_cluster = file_pos % cluster_size;
        uint64_t sector_index = offset_in_cluster / sizeof(sector);
        uint64_t sector_offset = offset_in_cluster % sizeof(sector);
        uint64_t to_copy = buffer_len - written;
        uint64_t available = sizeof(sector) - sector_offset;
        uint32_t cluster = 0;
        uint64_t lba;

        if (to_copy > available) {
            to_copy = available;
        }
        if (exfat_resolve_file_cluster(mount, entry, file_cluster_index, &cluster) != 0) {
            return (uint64_t)-1;
        }
        lba = exfat_cluster_lba(mount, cluster);
        if (lba == (uint64_t)-1 || sector_index >= sectors_per_cluster ||
            block_read(mount->block_index, lba + sector_index, 1, sector) != 0) {
            return (uint64_t)-1;
        }
        copy_bytes(sector + sector_offset, (const uint8_t *)buffer + written, to_copy);
        if (write_exfat_sector_checked_for_test(mount, lba + sector_index, sector) != 0) {
            return (uint64_t)-1;
        }
        written += to_copy;
    }

    if (offset + written > entry->size &&
        refresh_exfat_test_file_size_for_test(mount, entry, offset + written) != 0) {
        return (uint64_t)-1;
    }
    return written;
}

static uint64_t blockfs_write(const char *path, uint64_t offset,
                              const void *buffer, uint64_t buffer_len, void *context) {
    struct blockfs_mount *mount = context;
    struct exfat_root_entry *entry;

    if (mount == 0 || path == 0 || buffer == 0) {
        return (uint64_t)-1;
    }
    entry = find_exfat_root_entry(mount, path);
    if (entry == 0) {
        uint64_t requested_clusters;
        if (offset != 0) {
            return (uint64_t)-1;
        }
        requested_clusters = exfat_test_cluster_count_for_size(mount, buffer_len);
        if (requested_clusters == 0 ||
            create_exfat_test_file_with_clusters_for_test(
                mount,
                path,
                requested_clusters) != 0) {
            return (uint64_t)-1;
        }
        entry = find_exfat_root_entry(mount, path);
        if (entry == 0) {
            return (uint64_t)-1;
        }
    }
    return blockfs_write_new_file_for_test(mount, entry, offset, buffer, buffer_len);
}

static int blockfs_truncate(const char *path, uint64_t size, void *context) {
    struct blockfs_mount *mount = context;
    struct exfat_root_entry *entry;

    if (mount == 0 || path == 0) {
        return -1;
    }
    if (!mount->exfat.commit_ready) {
        return -1;
    }
    entry = find_exfat_root_entry(mount, path);
    if (entry == 0) {
        uint64_t requested_clusters =
            exfat_test_cluster_count_for_size(mount, size);
        if (requested_clusters == 0) {
            return -1;
        }
        if (rename_exfat_test_file_for_test(mount, path) != 0 &&
            create_exfat_test_file_with_clusters_for_test(
                mount,
                path,
                requested_clusters) != 0) {
            return -1;
        }
        entry = find_exfat_root_entry(mount, path);
        if (entry == 0) {
            return -1;
        }
    }
    if (!chars_equal(entry->parent, "/") ||
        !entry->writable ||
        size > entry->capacity ||
        entry->is_dir) {
        return -1;
    }
    return refresh_exfat_test_file_size_for_test(mount, entry, size);
}

static int blockfs_unlink(const char *path, void *context) {
    struct blockfs_mount *mount = context;

    return unlink_exfat_test_file_for_test(mount, path);
}
#endif

int blockfs_mount(uint64_t block_index, const char *mount_path) {
    struct blockfs_mount *mount = 0;
    uint8_t sector[512];
    vfs_write_fn write = 0;
    vfs_truncate_fn truncate = 0;
    vfs_unlink_fn unlink = 0;

    for (uint64_t i = 0; i < BLOCKFS_MAX_MOUNTS; i++) {
        if (!mounts[i].in_use) {
            mount = &mounts[i];
            break;
        }
    }
    if (mount == 0 || block_get_info(block_index, &mount->block) != 0) {
        return -1;
    }
    if (mount->block.block_size != 512) {
        log_warn("blkfs: unsupported block size on device %s\n", mount->block.name);
        return -1;
    }

    mount->in_use = 1;
    mount->block_index = block_index;
    copy_limited(mount->source, sizeof(mount->source), mount->block.name);
    zero_bytes(&mount->exfat, sizeof(mount->exfat));
    zero_bytes(&mount->fat32, sizeof(mount->fat32));
    reset_exfat_root_entries(mount);
    if (block_read(block_index, 0, 1, sector) == 0) {
        parse_exfat_boot_sector(mount, sector);
        if (mount->exfat.present) {
            log_info("exFAT boot sector detected on %s: bps=%u spc=%u fat=%u+%u heap=%u clusters=%u root=%u\n",
                     mount->block.name,
                     one_shifted_by(mount->exfat.bytes_per_sector_shift),
                     one_shifted_by(mount->exfat.sectors_per_cluster_shift),
                     mount->exfat.fat_offset,
                     mount->exfat.fat_length,
                     mount->exfat.cluster_heap_offset,
                     mount->exfat.cluster_count,
                     mount->exfat.root_dir_cluster);
            parse_exfat_root_directory(mount);
        }
        if (!mount->exfat.present) {
            parse_fat32_boot_sector(mount, sector);
            if (mount->fat32.present) {
                log_info("FAT32 boot sector detected on %s: bps=%u spc=%u fat=%u+%u root=%u total=%u\n",
                         mount->block.name,
                         (uint64_t)mount->fat32.bytes_per_sector,
                         (uint64_t)mount->fat32.sectors_per_cluster,
                         (uint64_t)mount->fat32.reserved_sectors,
                         (uint64_t)mount->fat32.fat_size_sectors,
                         (uint64_t)mount->fat32.root_dir_cluster,
                         (uint64_t)mount->fat32.total_sectors);
                parse_fat32_root_directory(mount);
            }
        }
    }

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    if (mount->exfat.present) {
        write = blockfs_write;
        truncate = blockfs_truncate;
        unlink = blockfs_unlink;
    }
#endif

    if (vfs_register_fs_mount("blkfs", mount_path, mount->source,
                              blockfs_read, blockfs_list, write,
                              blockfs_size, truncate, unlink, mount) != 0) {
        mount->in_use = 0;
        return -1;
    }

    log_info("blkfs mounted: %s at %s\n", mount->source, mount_path);
    return 0;
}
