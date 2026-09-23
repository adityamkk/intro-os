#pragma once

#include <cstdint>

// Reads raw sectors from the system's block device -- the first
// virtio-blk device QEMU exposes on its virtio-mmio bus (the Makefile
// attaches the FAT32 image built from a test's tests/<name>.dir there).
//
// All DiskReaders share that one device: constructing one probes for it
// on first use, and every read is serialized behind a spinlock, so
// instances are cheap and safe to create/use from any thread or core.
// Reads are polled (no interrupts) and copied through an internal bounce
// buffer, so `buf` may be any ordinary kernel memory, including a stack.
class DiskReader {
public:
    static constexpr uint32_t kSectorSize = 512;

    // Probes for the disk if no DiskReader has yet. Must not be called
    // before mmu::activate_this_core() (the virtio registers are MMIO).
    DiskReader();

    // True if a disk was found and initialized. When false, every read
    // fails.
    bool valid() const;

    // Total number of sectors on the disk (0 if !valid()).
    uint64_t sector_count() const;

    // Reads `count` consecutive sectors starting at `lba` into `buf`,
    // which must have room for count * kSectorSize bytes. Returns false
    // (leaving `buf` partly written) if there's no disk, the range runs
    // past the end of the disk, or the device fails or times out.
    bool read_sectors(uint64_t lba, uint32_t count, uint8_t *buf);
};
