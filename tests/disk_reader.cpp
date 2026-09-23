#include "test_util.hpp"

#include "disk_reader.hpp"
#include "string.hpp"

// Reads the FAT32 image the Makefile builds from tests/disk_reader.dir and
// checks its on-disk magic numbers, proving DiskReader really reads that
// filesystem (rather than zeros or garbage): the boot sector's 0x55AA
// signature and FAT32 BPB fields, the FSInfo sector's three signatures,
// and finally the root directory, which must list hello.txt from the
// .dir -- followed through its cluster to the file's actual contents.
namespace {

uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

alignas(16) uint8_t g_sector[DiskReader::kSectorSize];
alignas(16) uint8_t g_two[2 * DiskReader::kSectorSize];

} // namespace

extern "C" void main() {
    DiskReader disk;
    if (!disk.valid()) {
        test::fail("no disk found");
    }
    if (disk.sector_count() == 0) {
        test::fail("disk reports zero sectors");
    }

    // --- Boot sector / BPB ---
    if (!disk.read_sectors(0, 1, g_sector)) {
        test::fail("read of sector 0 failed");
    }
    if (g_sector[510] != 0x55 || g_sector[511] != 0xAA) {
        test::fail("boot sector signature != 0x55AA");
    }
    if (le16(g_sector + 11) != DiskReader::kSectorSize) {
        test::fail("BPB_BytsPerSec != 512");
    }
    if (le16(g_sector + 17) != 0 || le16(g_sector + 22) != 0) {
        test::fail("BPB_RootEntCnt/FATSz16 nonzero (not FAT32)");
    }
    if (le32(g_sector + 36) == 0) {
        test::fail("BPB_FATSz32 is zero");
    }
    if (memcmp(g_sector + 82, "FAT32   ", 8) != 0) {
        test::fail("BS_FilSysType != \"FAT32   \"");
    }

    uint32_t sectors_per_cluster = g_sector[13];
    uint32_t reserved = le16(g_sector + 14);
    uint32_t num_fats = g_sector[16];
    uint32_t fat_size = le32(g_sector + 36);
    uint32_t root_cluster = le32(g_sector + 44);
    uint32_t fsinfo_sector = le16(g_sector + 48);
    if (sectors_per_cluster == 0 || num_fats == 0 || root_cluster < 2) {
        test::fail("implausible BPB geometry");
    }

    // --- FSInfo sector ---
    if (!disk.read_sectors(fsinfo_sector, 1, g_sector)) {
        test::fail("read of FSInfo sector failed");
    }
    if (le32(g_sector + 0) != 0x41615252 || le32(g_sector + 484) != 0x61417272 ||
        le32(g_sector + 508) != 0xAA550000) {
        test::fail("FSInfo signatures wrong");
    }

    // --- Root directory: find HELLO.TXT's short-name entry ---
    uint32_t data_start = reserved + num_fats * fat_size;
    uint32_t root_sector = data_start + (root_cluster - 2) * sectors_per_cluster;
    if (!disk.read_sectors(root_sector, 2, g_two)) {
        test::fail("read of root directory failed");
    }
    const uint8_t *entry = nullptr;
    for (uint32_t off = 0; off + 32 <= sizeof(g_two); off += 32) {
        if (memcmp(g_two + off, "HELLO   TXT", 11) == 0) {
            entry = g_two + off;
            break;
        }
    }
    if (entry == nullptr) {
        test::fail("HELLO.TXT not in root directory");
    }

    // --- Follow it to the file's data ---
    static const char kExpected[] = "Hello from the FAT32 image!\n";
    uint32_t cluster = (static_cast<uint32_t>(le16(entry + 20)) << 16) | le16(entry + 26);
    if (le32(entry + 28) != sizeof(kExpected) - 1) {
        test::fail("HELLO.TXT has wrong size");
    }
    uint32_t file_sector = data_start + (cluster - 2) * sectors_per_cluster;
    if (!disk.read_sectors(file_sector, 1, g_sector)) {
        test::fail("read of file data failed");
    }
    if (memcmp(g_sector, kExpected, sizeof(kExpected) - 1) != 0) {
        test::fail("HELLO.TXT contents wrong");
    }

    // Out-of-range reads must be rejected, not wrap or hang.
    if (disk.read_sectors(disk.sector_count(), 1, g_sector)) {
        test::fail("read past end of disk succeeded");
    }

    test::pass();
}
