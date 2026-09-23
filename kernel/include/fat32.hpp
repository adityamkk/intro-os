#pragma once

#include <cstddef>
#include <cstdint>

#include "disk_reader.hpp"

class Fat32;

// Basic facts about a mounted FAT32 volume, taken from its boot sector
// (BPB) and FSInfo sector.
struct Fat32Metadata {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint64_t total_sectors;
    // First sector of the data region (cluster 2).
    uint32_t data_start_sector;
    // Number of data clusters; valid cluster numbers are 2..cluster_count+1.
    uint32_t cluster_count;
    uint32_t volume_id;
    // NUL-terminated volume label from the boot sector, trailing spaces
    // trimmed.
    char label[12];
    // FSInfo's cached free-cluster count. It's only a hint that the
    // filesystem's last writer maintained, and is 0xFFFFFFFF if
    // FSInfo says it's unknown.
    uint32_t free_clusters_hint;
};

// A handle to a file or directory on a Fat32 volume, returned by
// Fat32::find(). Cheap to copy, but reads update a small cursor cache
// (making sequential reads fast), so one File shouldn't be read from
// several threads at once -- copy it, or find() it again, per thread.
// It refers to its Fat32 by pointer, so that must outlive it.
class File {
public:
    // An invalid File (what find() returns when the path doesn't exist).
    File() = default;

    // True if this refers to something that exists.
    bool valid() const { return fs_ != nullptr; }
    bool is_directory() const { return dir_; }
    // Size in bytes; 0 for a directory (FAT doesn't record one).
    uint32_t size() const { return size_; }

    // Reads up to `len` bytes starting `offset` bytes into the file, into
    // `buf`. Returns the number of bytes read, which is less than `len`
    // only when the read hit the end of the file (0 if `offset` is at or
    // past it). Returns -1 for an invalid File, a directory, or a disk
    // error / corrupt cluster chain.
    int64_t read(uint64_t offset, void *buf, size_t len);

private:
    friend class Fat32;

    Fat32 *fs_ = nullptr;
    uint32_t first_cluster_ = 0; // 0 for an empty file
    uint32_t size_ = 0;
    bool dir_ = false;

    // Cursor cache: the cluster at chain position cache_index_. Lets a
    // read at or after that position resume rather than re-walk the FAT
    // chain from the start.
    uint32_t cache_index_ = 0;
    uint32_t cache_cluster_ = 0;
};

// Read-only FAT32 filesystem on top of a DiskReader (which it borrows, so
// it must outlive this). Handles long file names; lookups are
// case-insensitive, like FAT itself. Holds no mutable state after mount,
// so it's safe to use from several threads/cores at once.
class Fat32 {
public:
    // Mounts the volume: parses the boot sector and FSInfo. Check valid()
    // afterward.
    explicit Fat32(DiskReader &disk);

    // False if the disk is missing or doesn't hold a FAT32 filesystem.
    bool valid() const { return valid_; }

    // Only meaningful if valid().
    const Fat32Metadata &metadata() const { return meta_; }

    // Resolves an absolute path like "/docs/note.txt" ('/'-separated;
    // repeated and trailing slashes are ignored, "/" is the root
    // directory) to a File. Returns an invalid File if any component is
    // missing, or a non-final component isn't a directory.
    File find(const char *path);

private:
    friend class File;

    struct Entry {
        uint32_t first_cluster;
        uint32_t size;
        bool dir;
    };

    static constexpr uint32_t kEndOfChain = 0x0FFFFFF8;

    bool valid_ = false;
    DiskReader &disk_;
    Fat32Metadata meta_{};

    bool valid_cluster(uint32_t cluster) const {
        return cluster >= 2 && cluster < meta_.cluster_count + 2;
    }
    uint64_t cluster_lba(uint32_t cluster) const {
        return meta_.data_start_sector +
               static_cast<uint64_t>(cluster - 2) * meta_.sectors_per_cluster;
    }

    // Reads `cluster`'s FAT entry into `next` (masked to 28 bits).
    // Returns false on a disk error.
    bool fat_next(uint32_t cluster, uint32_t &next);

    // Searches directory `dir_cluster` for `name` (case-insensitive, matched
    // against long and short names). Returns false if not found or on error.
    bool lookup(uint32_t dir_cluster, const char *name, size_t name_len, Entry &out);
};
