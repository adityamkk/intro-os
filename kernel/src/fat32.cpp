#include "fat32.hpp"

#include "string.hpp"

// On-disk FAT32 structures (the boot sector/BPB, directory entries, FAT
// entries) are all just byte layouts, specified independently of any
// particular CPU's endianness or struct-packing rules. So rather than
// overlay a `struct` onto a raw sector (and fight the compiler over
// padding and byte order), everything here reads fields out of a raw
// `uint8_t*` at fixed offsets taken straight from the FAT32 spec, via the
// little bit of plumbing below.
namespace {

constexpr uint32_t kSector = DiskReader::kSectorSize;

// Directory entry attribute bits (byte 11 of a 32-byte directory entry).
constexpr uint8_t kAttrVolumeId = 0x08;
constexpr uint8_t kAttrDirectory = 0x10;
// All four of ReadOnly|Hidden|System|VolumeId set at once is how a long
// file name (LFN) entry disguises itself from software that doesn't know
// about them -- not a "real" attribute combination any file would have.
constexpr uint8_t kAttrLfn = 0x0F;
// Longest name find() will match; also bounds the LFN entries one name
// can spread across (20, each good for 13 UTF-16 code units).
constexpr size_t kMaxNameLen = 255;

// Little-endian field readers: every multi-byte field on a FAT32 volume
// is stored least-significant-byte first, regardless of the CPU reading
// it, so these can't just be a `*reinterpret_cast<uint16_t*>(p)`.
uint16_t le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

// FAT name matching is case-insensitive (there's no case-sensitive
// variant of a FAT volume), so every name comparison in this file goes
// through here rather than memcmp().
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

// The specific (if arbitrary) one-byte rolling checksum the FAT32 spec
// defines over an 11-byte 8.3 name. Every LFN entry for a long name
// carries the checksum of the 8.3 name generated alongside it, which is
// how lookup() ties a run of LFN entries to the short entry that follows
// them (and how it notices when they've gone stale -- see lookup()'s
// comments on 0xE5).
uint8_t short_name_checksum(const uint8_t *name11) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i]);
    }
    return sum;
}

// Renders an 8.3 directory-entry name ("HELLO   TXT", space-padded,
// 8-byte base + 3-byte extension, both upper-case on disk) as the
// familiar "HELLO.TXT". Returns its length; `out` needs room for 13 bytes
// (8 + '.' + 3 + no NUL -- callers track the length separately).
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
//
// A FAT32 volume's layout, front to back:
//
//   sector 0            the boot sector: a small bootstrap stub (we don't
//                        care about it) plus the BPB (BIOS Parameter
//                        Block), a table of fixed-offset fields describing
//                        the volume's geometry -- everything meta_ holds.
//   ...                  (unused "reserved" sectors, if reserved_sectors > 1)
//   reserved_sectors     the FAT region: num_fats back-to-back copies of
//                        the File Allocation Table (sectors_per_fat
//                        sectors each -- see fat_next()'s comments for
//                        what's actually in it). Only the first copy is
//                        ever read here; the second exists purely as a
//                        backup, which a read-only driver has no need for.
//   data_start_sector    the data region: every file and directory's
//                        actual bytes, carved up into clusters of
//                        sectors_per_cluster sectors apiece. Addressed by
//                        cluster number starting at 2 (0 and 1 are
//                        reserved; see cluster_lba()); the root directory
//                        lives here too, at meta_.root_cluster, rather
//                        than in some fixed location the way older FAT
//                        versions' root directories did.
//
// Mounting just means: read the boot sector, sanity-check it enough to
// reject non-FAT32 or corrupt media, pull the BPB fields out of it, and
// derive the handful of values (data_start_sector, cluster_count) that
// everything else in this file is built on.

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
    // Offsets below are the BPB's fixed field positions (BPB_BytsPerSec,
    // BPB_FATSz16, BPB_TotSec16, BPB_FATSz32, BS_FilSysType, in spec
    // terms) -- not something to derive, just where the spec puts them.
    if (sector[510] != 0x55 || sector[511] != 0xAA || le16(sector + 11) != kSector ||
        le16(sector + 17) != 0 || le16(sector + 22) != 0 || le32(sector + 36) == 0 ||
        memcmp(sector + 82, "FAT32   ", 8) != 0) {
        return;
    }

    // The rest of the geometry, same story: each field's offset and width
    // is dictated by the BPB layout, not by anything computed here.
    meta_.bytes_per_sector = kSector;
    meta_.sectors_per_cluster = sector[13]; // BPB_SecPerClus: 1 byte
    meta_.reserved_sectors = le16(sector + 14); // BPB_RsvdSecCnt
    meta_.num_fats = sector[16]; // BPB_NumFATs: 1 byte, almost always 2
    meta_.sectors_per_fat = le32(sector + 36); // BPB_FATSz32 (FAT32 only; no 16-bit field)
    meta_.root_cluster = le32(sector + 44); // BPB_RootClus
    // Total sector count is stored as a 16-bit field for small (FAT12/16)
    // volumes and a 32-bit field for larger ones; FAT32 volumes always
    // use the latter, but we check both, since nothing stops the 16-bit
    // one from holding a stale, overridden value instead of 0.
    uint32_t total16 = le16(sector + 19); // BPB_TotSec16
    meta_.total_sectors = total16 != 0 ? total16 : le32(sector + 32); // BPB_TotSec32
    meta_.volume_id = le32(sector + 67); // BS_VolID

    // BS_VolLab: 11 bytes, space-padded -- trim the padding, like
    // format_short_name() does for directory-entry names.
    size_t label_len = 11;
    while (label_len > 0 && sector[71 + label_len - 1] == ' ') {
        label_len--;
    }
    memcpy(meta_.label, sector + 71, label_len);
    meta_.label[label_len] = '\0';

    uint32_t fsinfo_sector = le16(sector + 48); // BPB_FSInfo

    // sectors_per_cluster must be a power of two per the spec (so cluster
    // math elsewhere can assume it), and dividing by zero further down
    // would be its own kind of bad time.
    if (meta_.sectors_per_cluster == 0 ||
        (meta_.sectors_per_cluster & (meta_.sectors_per_cluster - 1)) != 0 ||
        meta_.num_fats == 0 || meta_.reserved_sectors == 0) {
        return;
    }
    // Everything up through the FAT region's num_fats copies; the data
    // region -- and cluster 2 -- starts right after.
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

    // FSInfo is a second, optional sector (in the reserved area) that
    // caches a free-cluster count so tools don't have to scan the whole
    // FAT just to answer "how much space is left" -- a write-side
    // optimization this read-only driver has no real use for beyond
    // exposing it as a hint. Its own two magic numbers (at byte 0 and
    // 484) are unrelated to the boot sector's 0x55AA signature; a volume
    // that's missing or has a bad FSInfo is still a perfectly good
    // mountable FAT32 volume, just one we can't report a hint for.
    meta_.free_clusters_hint = 0xFFFFFFFF;
    if (fsinfo_sector != 0 && fsinfo_sector < meta_.reserved_sectors &&
        disk_.read_sectors(fsinfo_sector, 1, sector) && le32(sector) == 0x41615252 &&
        le32(sector + 484) == 0x61417272) {
        meta_.free_clusters_hint = le32(sector + 488);
    }

    valid_ = true;
}

//
// ---- What fat_next() needs to do -------------------------------------
//
// The File Allocation Table that gives FAT32 its name is, conceptually,
// just one giant array with one 32-bit entry per cluster: entry[N] says
// what comes after cluster N in whatever file or directory N belongs to.
// A file's data is rarely one contiguous run of clusters, so this is the
// linked list that lets the rest of this driver follow a file from its
// first cluster (found in its directory entry) to every cluster after
// it, one hop at a time, without caring where any of them actually sit
// on disk.
//
// On disk, that array lives at the very start of the FAT region (see the
// diagram above Fat32::Fat32()) -- sector meta_.reserved_sectors, right
// after the boot sector and its reserved siblings. Entry N is a plain
// 32-bit little-endian integer at byte offset N * 4 within that array,
// except only the low 28 bits are meaningful (the FAT32 spec reserves
// the top 4 for future use, so they must be masked off rather than
// trusted). A value in [kEndOfChain, 0x0FFFFFFF] marks the last cluster
// of a chain -- there's nothing after it -- though this driver only ever
// needs to notice that valid_cluster() rejects everything from
// kEndOfChain upward, so fat_next() itself doesn't need to special-case
// it.
//
// So: given `cluster`,
//   1. Compute that byte offset (cluster * 4) into the FAT array.
//   2. Convert it to a (sector, offset-within-sector) pair -- the FAT is
//      read through disk_.read_sectors(), which only deals in whole
//      512-byte (kSector) sectors, starting at meta_.reserved_sectors.
//   3. Read that one sector.
//   4. Pull the 32-bit little-endian value out at the right offset
//      (le32(), declared in the anonymous namespace above, does the
//      byte-order work) and mask it to 28 bits.
//   5. Write that into `next` and return true -- or return false if the
//      disk read itself failed.
//
// Parameters:
//   cluster: a cluster number already known to be valid (2 <=
//            cluster < meta_.cluster_count + 2 -- see valid_cluster()).
//            Callers are responsible for checking that before calling;
//            fat_next() does not need to.
//   next:    out-parameter set to cluster's FAT entry, masked to 28 bits,
//            on success. Untouched on failure.
// Returns false only if the underlying disk_.read_sectors() call fails
// (e.g. the computed sector is past the end of the disk); true otherwise.
// ============================================================================
bool Fat32::fat_next(uint32_t cluster, uint32_t &next) {
    uint32_t byte_off = cluster * 4;
    alignas(16) uint8_t sector[kSector];
    if (!disk_.read_sectors(meta_.reserved_sectors + byte_off / kSector, 1, sector)) {
        return false;
    }
    next = le32(sector + byte_off % kSector) & 0x0FFFFFFF;
    return true;
}

// A FAT32 directory is, physically, nothing special: just a file (or, for
// the root directory, a fixed cluster chain) whose bytes happen to be a
// packed array of 32-byte directory entries, read the same way any other
// file's clusters would be. Each entry is one of:
//
//   - a "short" (8.3) entry: byte 0 is the first character of an 11-byte
//     name (8-byte base + 3-byte extension, space-padded, upper-case),
//     byte 11 is the attribute byte, and the entry also carries the
//     file's starting cluster and size (read out at the bottom of this
//     function). Every real file and directory has exactly one of these.
//   - a "long file name" (LFN) entry: disguised as a short entry with
//     attribute byte kAttrLfn (0x0F, a combination no real file has), but
//     actually holding 13 UTF-16 characters of a longer name that
//     wouldn't fit in 8.3. A name longer than 13 characters spans
//     several of these, stored *before* their short entry and in
//     *reverse* order (last piece first) -- see the ordinal handling
//     below.
//   - free space: a first byte of 0x00 means every entry from here to the
//     end of this cluster (and, as this driver assumes, every cluster
//     after it -- directories don't have holes) is unused, i.e. this is
//     the end of the directory. A first byte of 0xE5 means just this one
//     entry was deleted; the slot may be reused later, but for now it's
//     skipped. Deleting a short entry doesn't necessarily clean up its
//     LFN pieces, which is one reason they're re-validated via checksum
//     (see have_lfn below) rather than trusted on their own.
//
// This function walks dir_cluster's cluster chain one directory entry at
// a time, reassembling any LFN pieces it finds into `lfn` as it goes, and
// comparing the resulting name against `name` at each short entry
// (falling back to the short name itself if there's no valid pending LFN
// -- a name that's already 8.3-sized often isn't given any LFN entries at
// all, and even a true long name is only matchable this way if it
// doesn't happen to be what the caller is asking for).
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
            // Directory entries are 32 bytes each, packed back to back.
            for (uint32_t off = 0; off < kSector; off += 32) {
                const uint8_t *e = sector + off;
                if (e[0] == 0x00) {
                    return false; // end of directory
                }
                if (e[0] == 0xE5) {
                    lfn_ok = false; // a deleted entry invalidates any pending LFN pieces
                    continue;
                }

                uint8_t attr = e[11];
                if (attr == kAttrLfn) {
                    // Byte 0 doubles as an "ordinal": bits 0-5 are this
                    // piece's 1-based position (bigger = earlier in the
                    // name, since pieces are stored last-first), and bit
                    // 6 (0x40) marks the last piece -- i.e. the first one
                    // encountered on disk, which is also where this
                    // piece's checksum of the upcoming short name lives
                    // (byte 13), the same checksum every other piece of
                    // this name must also carry.
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
                        // Either we're not mid-assembly of a name (no
                        // "last piece" seen yet, or something broke the
                        // run) or this piece's checksum doesn't match the
                        // one the run started with -- either way, this
                        // piece doesn't belong to a name we can trust.
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

                // A short entry always follows its LFN pieces (if any)
                // immediately, so this is the first short entry seen
                // since the last "last piece" -- but it's only actually
                // *this* name's LFN if its checksum of the name below
                // matches what the LFN run carried; otherwise the run
                // belonged to an entry that's since been deleted/moved
                // and this is unrelated leftover state.
                bool have_lfn = lfn_ok && short_name_checksum(e) == lfn_checksum;
                lfn_ok = false;
                if (attr & kAttrVolumeId) {
                    continue; // the volume label can appear as a root-directory entry
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
                    // The short entry's starting cluster is split across
                    // two non-adjacent 16-bit fields -- a holdover from
                    // FAT16, where the high half (bytes 20-21) didn't
                    // exist yet and this low half (bytes 26-27) was the
                    // whole thing.
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

// Resolves `path` one '/'-separated component at a time, starting from
// the root and calling lookup() once per component -- so "/docs/note.txt"
// costs two lookup() calls: one for "docs" in the root directory, one for
// "note.txt" in whatever directory that resolved to. `cur` tracks where
// the walk currently stands; it starts out as a synthetic entry for the
// root itself (first_cluster = meta_.root_cluster, dir = true) so the
// very first lookup() call has somewhere to search.
File Fat32::find(const char *path) {
    File none;
    if (!valid_ || path == nullptr) {
        return none;
    }

    Entry cur{meta_.root_cluster, 0, true};
    const char *p = path;
    for (;;) {
        // Skip any run of slashes (so repeated/leading ones are ignored),
        // then take everything up to the next slash (or the end of the
        // string) as this component's name.
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break; // no more components -- `cur` is the answer
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

// Reading a file is: clamp the request to the file's actual size, then
// repeatedly figure out which cluster covers the current position,
// locate it in the cluster chain (walking forward via fat_next() from
// either the start of the file or, more often, a cached position --
// see below), and copy as much as possible out of it before moving on
// to wherever the chain goes next. "As much as possible" splits into two
// cases, handled by the if/else near the bottom of the loop:
//
//   - If the current position is sector-aligned and at least a whole
//     sector remains to read, skip the bounce buffer entirely and read
//     as many whole sectors as fit directly into the caller's buffer --
//     but only up to the end of the *current* cluster, since the next
//     cluster in the chain isn't necessarily the next sector on disk.
//   - Otherwise (a sub-sector read, or the leading/trailing partial
//     sector of a larger one), read one sector into a bounce buffer and
//     copy out just the bytes actually wanted. This is also what keeps
//     the fast path above safe: it only ever triggers once `within` is
//     exactly 0, i.e. the previous iteration (whether fast- or slow-path)
//     left `done` sitting on a sector boundary.
//
// Chain position (`index`, a cluster count from the start of the file)
// is turned into an actual cluster number by walking fat_next() forward
// one hop at a time from some earlier known point in the chain. Doing
// that from cluster 0 of the file on every call would make reading a
// large file an O(n^2) number of FAT lookups (each byte read re-walking
// everything before it), so each File caches the last cluster index/
// number pair it resolved (cache_index_/cache_cluster_) and resumes from
// there whenever the new target is at or after it -- which covers the
// overwhelmingly common case of sequential or forward-seeking reads.
// Seeking backward still works, just by falling back to walking from the
// start again.
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
        uint64_t index = pos / cluster_bytes; // which cluster (by chain position) `pos` falls in

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
        // Walk forward one FAT link (fat_next(), the assignment function
        // above) per remaining hop until `cluster` is the one at chain
        // position `index`.
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

        // Where `pos` falls inside this cluster, as a (sector, byte)
        // pair, and the disk sector that corresponds to.
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
            // Partial sector (the read's leading or trailing edge, or a
            // request smaller than one sector): go through the bounce
            // buffer and copy out only the bytes actually wanted, since
            // disk_.read_sectors() can't write a sub-sector range.
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
