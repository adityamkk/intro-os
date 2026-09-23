#include "disk_reader.hpp"

#include "mmu.hpp"
#include "spinlock.hpp"
#include "string.hpp"

// A minimal polled virtio-blk driver over virtio-mmio (spec 1.x, "modern"
// version-2 register layout -- the Makefile passes
// `-global virtio-mmio.force-legacy=false` so QEMU speaks that rather
// than the legacy v1 layout).

namespace {

// QEMU's virt machine places 32 virtio-mmio transports back to back here.
constexpr uint64_t kMmioBase = 0x0a000000;
constexpr uint64_t kMmioStride = 0x200;
constexpr int kMmioSlots = 32;

// Register offsets.
constexpr uint32_t kRegMagic = 0x000;
constexpr uint32_t kRegVersion = 0x004;
constexpr uint32_t kRegDeviceId = 0x008;
constexpr uint32_t kRegDeviceFeatures = 0x010;
constexpr uint32_t kRegDeviceFeaturesSel = 0x014;
constexpr uint32_t kRegDriverFeatures = 0x020;
constexpr uint32_t kRegDriverFeaturesSel = 0x024;
constexpr uint32_t kRegQueueSel = 0x030;
constexpr uint32_t kRegQueueNumMax = 0x034;
constexpr uint32_t kRegQueueNum = 0x038;
constexpr uint32_t kRegQueueReady = 0x044;
constexpr uint32_t kRegQueueNotify = 0x050;
constexpr uint32_t kRegInterruptStatus = 0x060;
constexpr uint32_t kRegInterruptAck = 0x064;
constexpr uint32_t kRegStatus = 0x070;
constexpr uint32_t kRegQueueDescLow = 0x080;
constexpr uint32_t kRegQueueDriverLow = 0x090;
constexpr uint32_t kRegQueueDeviceLow = 0x0a0;
constexpr uint32_t kRegConfig = 0x100; // virtio-blk: u64 capacity, in sectors

constexpr uint32_t kMagicValue = 0x74726976; // "virt"
constexpr uint32_t kDeviceIdBlock = 2;

constexpr uint32_t kStatusAcknowledge = 1;
constexpr uint32_t kStatusDriver = 2;
constexpr uint32_t kStatusDriverOk = 4;
constexpr uint32_t kStatusFeaturesOk = 8;

constexpr uint32_t kFeatureVersion1Word1 = 1; // VIRTIO_F_VERSION_1 (bit 32) in word 1

constexpr uint16_t kDescFNext = 1;
constexpr uint16_t kDescFWrite = 2; // device writes to this buffer

constexpr uint32_t kBlkTIn = 0; // read
constexpr uint8_t kBlkSOk = 0;

constexpr uint16_t kQueueSize = 8;
constexpr uint64_t kPollLimit = 200000000;

struct Desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct Avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[kQueueSize];
};

struct UsedElem {
    uint32_t id;
    uint32_t len;
};

struct Used {
    uint16_t flags;
    uint16_t idx;
    UsedElem ring[kQueueSize];
};

struct ReqHeader {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

// Everything the device DMAs to/from lives in this one static object, so
// mmu::virt_to_phys() covers it all.
struct alignas(4096) Dma {
    alignas(16) Desc desc[kQueueSize];
    alignas(2) Avail avail;
    alignas(4) Used used;
    alignas(16) ReqHeader header;
    volatile uint8_t status;
    alignas(16) uint8_t data[DiskReader::kSectorSize];
};

Dma g_dma;

Spinlock g_lock;
bool g_probed = false;
bool g_valid = false;
uint64_t g_base = 0;
uint64_t g_capacity = 0;
uint16_t g_last_used = 0;

uint32_t reg_read(uint32_t off) {
    return *reinterpret_cast<volatile uint32_t *>(g_base + off);
}

void reg_write(uint32_t off, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(g_base + off) = value;
}

void write_addr(uint32_t low_off, const void *p) {
    uint64_t phys = mmu::virt_to_phys(p);
    reg_write(low_off, static_cast<uint32_t>(phys));
    reg_write(low_off + 4, static_cast<uint32_t>(phys >> 32));
}

// Scans the virtio-mmio slots for a modern block device. On success
// leaves g_base pointing at it.
bool find_device() {
    for (int i = 0; i < kMmioSlots; i++) {
        g_base = kMmioBase + static_cast<uint64_t>(i) * kMmioStride;
        if (reg_read(kRegMagic) == kMagicValue && reg_read(kRegVersion) == 2 &&
            reg_read(kRegDeviceId) == kDeviceIdBlock) {
            return true;
        }
    }
    return false;
}

bool init_device() {
    if (!find_device()) {
        return false;
    }

    reg_write(kRegStatus, 0); // reset
    reg_write(kRegStatus, kStatusAcknowledge);
    reg_write(kRegStatus, kStatusAcknowledge | kStatusDriver);

    // Accept only VIRTIO_F_VERSION_1; we need none of the optional features.
    reg_write(kRegDeviceFeaturesSel, 1);
    if (!(reg_read(kRegDeviceFeatures) & kFeatureVersion1Word1)) {
        return false;
    }
    reg_write(kRegDriverFeaturesSel, 0);
    reg_write(kRegDriverFeatures, 0);
    reg_write(kRegDriverFeaturesSel, 1);
    reg_write(kRegDriverFeatures, kFeatureVersion1Word1);

    uint32_t status = kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk;
    reg_write(kRegStatus, status);
    if (!(reg_read(kRegStatus) & kStatusFeaturesOk)) {
        return false;
    }

    reg_write(kRegQueueSel, 0);
    if (reg_read(kRegQueueNumMax) < kQueueSize) {
        return false;
    }
    reg_write(kRegQueueNum, kQueueSize);
    write_addr(kRegQueueDescLow, g_dma.desc);
    write_addr(kRegQueueDriverLow, &g_dma.avail);
    write_addr(kRegQueueDeviceLow, &g_dma.used);
    reg_write(kRegQueueReady, 1);

    reg_write(kRegStatus, status | kStatusDriverOk);

    g_capacity = static_cast<uint64_t>(reg_read(kRegConfig)) |
                 (static_cast<uint64_t>(reg_read(kRegConfig + 4)) << 32);
    return true;
}

// Reads one sector into g_dma.data. Caller holds g_lock.
bool read_one(uint64_t lba) {
    g_dma.header = {kBlkTIn, 0, lba};
    g_dma.status = 0xff;

    g_dma.desc[0] = {mmu::virt_to_phys(&g_dma.header), sizeof(ReqHeader), kDescFNext, 1};
    g_dma.desc[1] = {mmu::virt_to_phys(g_dma.data), DiskReader::kSectorSize,
                     static_cast<uint16_t>(kDescFNext | kDescFWrite), 2};
    g_dma.desc[2] = {mmu::virt_to_phys(const_cast<uint8_t *>(&g_dma.status)), 1, kDescFWrite, 0};

    uint16_t avail_idx = g_dma.avail.idx;
    g_dma.avail.ring[avail_idx % kQueueSize] = 0;
    asm volatile("dsb sy" ::: "memory");
    *const_cast<volatile uint16_t *>(&g_dma.avail.idx) = static_cast<uint16_t>(avail_idx + 1);
    asm volatile("dsb sy" ::: "memory");
    reg_write(kRegQueueNotify, 0);

    volatile uint16_t *used_idx = &g_dma.used.idx;
    uint64_t spins = 0;
    while (*used_idx == g_last_used) {
        if (++spins > kPollLimit) {
            return false;
        }
    }
    g_last_used++;
    asm volatile("dsb sy" ::: "memory");

    reg_write(kRegInterruptAck, reg_read(kRegInterruptStatus));
    return g_dma.status == kBlkSOk;
}

// Runs the one-time probe. Caller holds g_lock.
void probe_locked() {
    if (!g_probed) {
        g_valid = init_device();
        g_probed = true;
    }
}

} // namespace

DiskReader::DiskReader() {
    g_lock.lock();
    probe_locked();
    g_lock.unlock();
}

bool DiskReader::valid() const { return g_valid; }

uint64_t DiskReader::sector_count() const { return g_valid ? g_capacity : 0; }

bool DiskReader::read_sectors(uint64_t lba, uint32_t count, uint8_t *buf) {
    if (!g_valid || lba > g_capacity || count > g_capacity - lba) {
        return false;
    }

    g_lock.lock();
    bool ok = true;
    for (uint32_t i = 0; i < count && ok; i++) {
        ok = read_one(lba + i);
        if (ok) {
            memcpy(buf + static_cast<uint64_t>(i) * kSectorSize, g_dma.data, kSectorSize);
        }
    }
    g_lock.unlock();
    return ok;
}
