#include "test_util.hpp"

#include "disk_reader.hpp"
#include "fat32.hpp"
#include "string.hpp"

// Exercises Fat32::find() and File::read() against the image built from
// tests/fat32.dir: short and long names, case-insensitive and nested
// paths, missing paths, and reads at assorted offsets and lengths --
// including ones that straddle sector and cluster boundaries (the image
// uses 1-sector clusters) and run off the end of a file.
namespace {

// big.txt is 600 lines of "line NNNN\n" (10 bytes each): byte i is fully
// determined by i, so any window of it can be checked without a copy.
constexpr uint32_t kBigLines = 600;
constexpr uint32_t kBigSize = kBigLines * 10;

char big_byte(uint32_t i) {
    uint32_t line = i / 10;
    uint32_t col = i % 10;
    if (col < 5) {
        return "line "[col];
    }
    if (col == 9) {
        return '\n';
    }
    uint32_t div = col == 5 ? 1000 : col == 6 ? 100 : col == 7 ? 10 : 1;
    return static_cast<char>('0' + (line / div) % 10);
}

alignas(16) uint8_t g_buf[kBigSize + 64];

// Reads `len` bytes at `offset` of big.txt and checks them against
// big_byte(); `expect` is the byte count read() should return.
void check_big(File &f, uint32_t offset, size_t len, int64_t expect, const char *what) {
    memset(g_buf, 0xEE, sizeof(g_buf));
    int64_t n = f.read(offset, g_buf, len);
    if (n != expect) {
        test::fail(what);
    }
    for (int64_t i = 0; i < n; i++) {
        if (g_buf[i] != static_cast<uint8_t>(big_byte(offset + static_cast<uint32_t>(i)))) {
            test::fail(what);
        }
    }
    // Nothing past what was reported may be touched.
    if (g_buf[n] != 0xEE) {
        test::fail("read wrote past its reported length");
    }
}

size_t len_of(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void check_text(Fat32 &fs, const char *path, const char *expected) {
    File f = fs.find(path);
    size_t len = len_of(expected);
    if (!f.valid() || f.is_directory() || f.size() != len) {
        test::fail(path);
    }
    memset(g_buf, 0, sizeof(g_buf));
    if (f.read(0, g_buf, sizeof(g_buf)) != static_cast<int64_t>(len) ||
        memcmp(g_buf, expected, len) != 0) {
        test::fail(path);
    }
}

} // namespace

extern "C" void main() {
    DiskReader disk;
    Fat32 fs(disk);
    if (!fs.valid()) {
        test::fail("mount failed");
    }

    // --- Metadata ---
    const Fat32Metadata &m = fs.metadata();
    if (m.bytes_per_sector != 512 || m.sectors_per_cluster != 1 || m.num_fats != 2 ||
        m.root_cluster != 2 || m.total_sectors != disk.sector_count() ||
        m.data_start_sector != m.reserved_sectors + m.num_fats * m.sectors_per_fat ||
        m.cluster_count < 65525 || m.cluster_count > m.total_sectors) {
        test::fail("metadata geometry wrong");
    }
    if (memcmp(m.label, "INTROOS", 8) != 0) {
        test::fail("metadata label wrong");
    }
    // 4 dirs/files worth of clusters are in use, so the hint (if the
    // formatter recorded one) must be a plausible count below the total.
    if (m.free_clusters_hint != 0xFFFFFFFF && m.free_clusters_hint > m.cluster_count) {
        test::fail("metadata free-cluster hint implausible");
    }

    // --- find() ---
    File root = fs.find("/");
    if (!root.valid() || !root.is_directory() || root.read(0, g_buf, 1) != -1) {
        test::fail("root directory wrong");
    }
    if (!fs.find("").is_directory() || !fs.find("//").is_directory()) {
        test::fail("empty path should be the root");
    }

    check_text(fs, "/hello.txt", "Hello from the FAT32 image!\n");
    check_text(fs, "/HELLO.TXT", "Hello from the FAT32 image!\n");
    check_text(fs, "hello.txt", "Hello from the FAT32 image!\n");
    check_text(fs, "/docs/note.txt", "nested\n");
    check_text(fs, "//docs//Note.TXT", "nested\n");
    check_text(fs, "/docs/deeply/A Rather Long File Name.txt", "a long file name\n");
    check_text(fs, "/DOCS/DEEPLY/a rather long file name.TXT", "a long file name\n");

    File docs = fs.find("/docs/");
    if (!docs.valid() || !docs.is_directory()) {
        test::fail("/docs/ should be a directory");
    }
    if (fs.find("/nope.txt").valid() || fs.find("/docs/nope").valid() ||
        fs.find("/nope/note.txt").valid()) {
        test::fail("missing path found");
    }
    if (fs.find("/hello.txt/x").valid()) {
        test::fail("descended into a file");
    }
    if (fs.find("/docs/deeply/A Rather Long File Name").valid()) {
        test::fail("long-name prefix matched");
    }

    // --- read() ---
    File empty = fs.find("/empty.txt");
    if (!empty.valid() || empty.size() != 0 || empty.read(0, g_buf, 16) != 0) {
        test::fail("empty file wrong");
    }

    File big = fs.find("/big.txt");
    if (!big.valid() || big.is_directory() || big.size() != kBigSize) {
        test::fail("big.txt wrong size");
    }
    check_big(big, 0, kBigSize, kBigSize, "whole-file read");
    check_big(big, 0, 1, 1, "first byte");
    check_big(big, 3, 7, 7, "small read");
    check_big(big, 505, 20, 20, "read across a sector boundary");
    check_big(big, 511, 2, 2, "read straddling sector 0/1");
    check_big(big, 512, 512, 512, "aligned whole-sector read");
    check_big(big, 100, 2000, 2000, "unaligned multi-sector read");
    check_big(big, 1024, 1536, 1536, "aligned multi-sector read");
    check_big(big, kBigSize - 5, 5, 5, "tail read");
    check_big(big, kBigSize - 5, 100, 5, "read clamped at EOF");
    check_big(big, kBigSize, 10, 0, "read at EOF");
    check_big(big, kBigSize + 100, 10, 0, "read past EOF");
    check_big(big, 0, 0, 0, "zero-length read");
    // Going backwards after a forward read (the cursor cache must reset).
    check_big(big, 5000, 100, 100, "late read");
    check_big(big, 10, 100, 100, "earlier read after later one");
    // Sequential small reads, like a consumer streaming the file.
    for (uint32_t off = 0; off < kBigSize; off += 77) {
        check_big(big, off, 77, off + 77 <= kBigSize ? 77 : kBigSize - off, "streaming read");
    }

    test::pass();
}
