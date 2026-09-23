#include "fat32.hpp"

#include "string.hpp"

namespace {

constexpr uint32_t kSector = DiskReader::kSectorSize;
constexpr uint8_t kAttrVolumeId = 0x08;
constexpr uint8_t kAttrDirectory = 0x10;
constexpr uint8_t kAttrLfn = 0x0F;
constexpr size_t kMaxNameLen = 255;

uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

bool equal_nocase(const char *a, size_t alen, const char *b, size_t blen) {
    if (alen != blen) {
        return false;
    }
    for (size_t i = 0; i < alen; i++) {
        if (upper(a[i]) != upper(b[i])) {
            return false;
        }
    }
    return true;
}

// Checksum of an 8.3 name that its long-name entries carry, tying them to it.
uint8_t short_name_checksum(const uint8_t *name11) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i]);
    }
    return sum;
}

// Renders an 8.3 directory-entry name ("HELLO   TXT") as "HELLO.TXT".
// Returns its length; `out` needs room for 13 bytes.
size_t format_short_name(const uint8_t *name11, char *out) {
    size_t n = 0;
    for (int i = 0; i < 8 && name11[i] != ' '; i++) {
        // 0x05 stands in for a real leading 0xE5 byte.
        out[n++] = static_cast<char>((i == 0 && name11[i] == 0x05) ? 0xE5 : name11[i]);
    }
    if (name11[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && name11[i] != ' '; i++) {
            out[n++] = static_cast<char>(name11[i]);
        }
    }
    return n;
}

} // namespace

// ---- Fat32 --------------------------------------------------------------

Fat32::Fat32(DiskReader &disk) : disk_(disk) {
    if (!disk_.valid()) {
        return;
    }

    alignas(16) uint8_t sector[kSector];
    if (!disk_.read_sectors(0, 1, sector)) {
        return;
    }

    // A FAT32 BPB has no fixed root directory or 16-bit FAT size, and the
    // "FAT32   " type label -- informational per the spec, but exactly what
    // mkfs tools write, and cheap insurance against mounting garbage.
    if (sector[510] != 0x55 || sector[511] != 0xAA || le16(sector + 11) != kSector ||
        le16(sector + 17) != 0 || le16(sector + 22) != 0 || le32(sector + 36) == 0 ||
        memcmp(sector + 82, "FAT32   ", 8) != 0) {
        return;
    }

    meta_.bytes_per_sector = kSector;
    meta_.sectors_per_cluster = sector[13];
    meta_.reserved_sectors = le16(sector + 14);
    meta_.num_fats = sector[16];
    meta_.sectors_per_fat = le32(sector + 36);
    meta_.root_cluster = le32(sector + 44);
    uint32_t total16 = le16(sector + 19);
    meta_.total_sectors = total16 != 0 ? total16 : le32(sector + 32);
    meta_.volume_id = le32(sector + 67);

    size_t label_len = 11;
    while (label_len > 0 && sector[71 + label_len - 1] == ' ') {
        label_len--;
    }
    memcpy(meta_.label, sector + 71, label_len);
    meta_.label[label_len] = '\0';

    uint32_t fsinfo_sector = le16(sector + 48);

    if (meta_.sectors_per_cluster == 0 ||
        (meta_.sectors_per_cluster & (meta_.sectors_per_cluster - 1)) != 0 ||
        meta_.num_fats == 0 || meta_.reserved_sectors == 0) {
        return;
    }
    uint64_t data_start = meta_.reserved_sectors +
                          static_cast<uint64_t>(meta_.num_fats) * meta_.sectors_per_fat;
    if (data_start >= meta_.total_sectors || meta_.total_sectors > disk_.sector_count()) {
        return;
    }
    meta_.data_start_sector = static_cast<uint32_t>(data_start);
    meta_.cluster_count = static_cast<uint32_t>((meta_.total_sectors - data_start) /
                                                meta_.sectors_per_cluster);
    if (!valid_cluster(meta_.root_cluster)) {
        return;
    }

    meta_.free_clusters_hint = 0xFFFFFFFF;
    if (fsinfo_sector != 0 && fsinfo_sector < meta_.reserved_sectors &&
        disk_.read_sectors(fsinfo_sector, 1, sector) && le32(sector) == 0x41615252 &&
        le32(sector + 484) == 0x61417272) {
        meta_.free_clusters_hint = le32(sector + 488);
    }

    valid_ = true;
}

bool Fat32::fat_next(uint32_t cluster, uint32_t &next) {
    uint32_t byte_off = cluster * 4;
    alignas(16) uint8_t sector[kSector];
    if (!disk_.read_sectors(meta_.reserved_sectors + byte_off / kSector, 1, sector)) {
        return false;
    }
    next = le32(sector + byte_off % kSector) & 0x0FFFFFFF;
    return true;
}

bool Fat32::lookup(uint32_t dir_cluster, const char *name, size_t name_len, Entry &out) {
    // Long-name state: entries for one name come just before its 8.3 entry,
    // last piece first.
    char lfn[kMaxNameLen + 14];
    size_t lfn_len = 0;
    bool lfn_ok = false;
    uint8_t lfn_checksum = 0;

    alignas(16) uint8_t sector[kSector];
    uint32_t cluster = dir_cluster;

    // The bound also stops a corrupt, cyclic chain from looping forever.
    for (uint32_t hops = 0; valid_cluster(cluster) && hops <= meta_.cluster_count; hops++) {
        for (uint32_t s = 0; s < meta_.sectors_per_cluster; s++) {
            if (!disk_.read_sectors(cluster_lba(cluster) + s, 1, sector)) {
                return false;
            }
            for (uint32_t off = 0; off < kSector; off += 32) {
                const uint8_t *e = sector + off;
                if (e[0] == 0x00) {
                    return false; // end of directory
                }
                if (e[0] == 0xE5) {
                    lfn_ok = false;
                    continue;
                }

                uint8_t attr = e[11];
                if (attr == kAttrLfn) {
                    uint8_t ord = e[0];
                    uint32_t seq = ord & 0x3F;
                    if (seq == 0 || seq * 13 > kMaxNameLen + 13) {
                        lfn_ok = false;
                        continue;
                    }
                    if (ord & 0x40) { // last piece: comes first on disk
                        memset(lfn, 0, sizeof(lfn));
                        lfn_len = seq * 13;
                        lfn_checksum = e[13];
                        lfn_ok = true;
                    } else if (!lfn_ok || e[13] != lfn_checksum) {
                        lfn_ok = false;
                        continue;
                    }
                    // Characters live at these offsets in the entry, as UTF-16;
                    // anything past ASCII is kept as '?' (never matches a name
                    // a caller can spell).
                    static const uint8_t kOffsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                    for (int i = 0; i < 13; i++) {
                        uint16_t ch = le16(e + kOffsets[i]);
                        lfn[(seq - 1) * 13 + i] =
                            ch == 0xFFFF ? '\0' : (ch < 0x80 ? static_cast<char>(ch) : '?');
                    }
                    continue;
                }

                bool have_lfn = lfn_ok && short_name_checksum(e) == lfn_checksum;
                lfn_ok = false;
                if (attr & kAttrVolumeId) {
                    continue;
                }

                bool match = false;
                if (have_lfn) {
                    size_t n = 0;
                    while (n < lfn_len && lfn[n] != '\0') {
                        n++;
                    }
                    match = equal_nocase(lfn, n, name, name_len);
                }
                if (!match) {
                    char shortname[13];
                    size_t n = format_short_name(e, shortname);
                    match = equal_nocase(shortname, n, name, name_len);
                }
                if (match) {
                    out.first_cluster = (static_cast<uint32_t>(le16(e + 20)) << 16) | le16(e + 26);
                    out.size = le32(e + 28);
                    out.dir = (attr & kAttrDirectory) != 0;
                    return true;
                }
            }
        }

        uint32_t next;
        if (!fat_next(cluster, next)) {
            return false;
        }
        cluster = next;
    }
    return false;
}

File Fat32::find(const char *path) {
    File none;
    if (!valid_ || path == nullptr) {
        return none;
    }

    Entry cur{meta_.root_cluster, 0, true};
    const char *p = path;
    for (;;) {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *end = p;
        while (*end != '\0' && *end != '/') {
            end++;
        }

        // Can't descend into a file. (A directory's cluster is never 0 here:
        // ".." entries pointing at the root store 0 for it.)
        if (!cur.dir) {
            return none;
        }
        size_t len = static_cast<size_t>(end - p);
        if (len > kMaxNameLen) {
            return none;
        }
        uint32_t dir_cluster = cur.first_cluster == 0 ? meta_.root_cluster : cur.first_cluster;
        if (!lookup(dir_cluster, p, len, cur)) {
            return none;
        }
        p = end;
    }

    File f;
    f.fs_ = this;
    f.first_cluster_ = cur.dir && cur.first_cluster == 0 ? meta_.root_cluster : cur.first_cluster;
    f.size_ = cur.dir ? 0 : cur.size;
    f.dir_ = cur.dir;
    return f;
}

// ---- File ---------------------------------------------------------------

int64_t File::read(uint64_t offset, void *buf, size_t len) {
    if (fs_ == nullptr || dir_) {
        return -1;
    }
    if (offset >= size_ || len == 0) {
        return 0;
    }
    if (len > size_ - offset) {
        len = static_cast<size_t>(size_ - offset);
    }

    const Fat32Metadata &m = fs_->meta_;
    const uint64_t cluster_bytes = static_cast<uint64_t>(m.sectors_per_cluster) * kSector;
    auto *out = static_cast<uint8_t *>(buf);
    alignas(16) uint8_t sector[kSector];

    size_t done = 0;
    while (done < len) {
        uint64_t pos = offset + done;
        uint64_t index = pos / cluster_bytes;

        // Find the cluster at chain position `index`, resuming from the
        // cached position when it's not past the target.
        uint32_t cluster;
        uint64_t at;
        if (fs_->valid_cluster(cache_cluster_) && cache_index_ <= index) {
            cluster = cache_cluster_;
            at = cache_index_;
        } else {
            cluster = first_cluster_;
            at = 0;
        }
        while (at < index) {
            uint32_t next;
            if (!fs_->valid_cluster(cluster) || !fs_->fat_next(cluster, next)) {
                return -1;
            }
            cluster = next;
            at++;
        }
        if (!fs_->valid_cluster(cluster)) {
            return -1; // chain ended before the file's recorded size
        }
        cache_index_ = static_cast<uint32_t>(index);
        cache_cluster_ = cluster;

        uint64_t within = pos % cluster_bytes;
        uint32_t sector_in_cluster = static_cast<uint32_t>(within / kSector);
        uint32_t byte_in_sector = static_cast<uint32_t>(within % kSector);
        uint64_t lba = fs_->cluster_lba(cluster) + sector_in_cluster;
        size_t remaining = len - done;

        if (byte_in_sector == 0 && remaining >= kSector) {
            // Whole sectors: read straight into the caller's buffer, as many
            // as fit before the cluster ends (the next one may be elsewhere).
            uint32_t n = static_cast<uint32_t>(remaining / kSector);
            uint32_t left_in_cluster = m.sectors_per_cluster - sector_in_cluster;
            if (n > left_in_cluster) {
                n = left_in_cluster;
            }
            if (!fs_->disk_.read_sectors(lba, n, out + done)) {
                return -1;
            }
            done += static_cast<size_t>(n) * kSector;
        } else {
            size_t chunk = kSector - byte_in_sector;
            if (chunk > remaining) {
                chunk = remaining;
            }
            if (!fs_->disk_.read_sectors(lba, 1, sector)) {
                return -1;
            }
            memcpy(out + done, sector + byte_in_sector, chunk);
            done += chunk;
        }
    }
    return static_cast<int64_t>(done);
}
