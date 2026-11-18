#include "test_util.hpp"

#include "disk_reader.hpp"
#include "fat32.hpp"
#include "kmmap.hpp"
#include "pagetable.hpp"
#include "threads.hpp"

// Exercises kmmap()/kmunmap(): each thread gets its own address space, so
// two threads mapping the exact same virtual address must never see each
// other's data (the explicit point of this assignment), and kmunmap()'s
// delete/shrink/split bookkeeping must leave the page table and the
// surviving VMEs consistent.
namespace {

constexpr int kSpinRounds = 4000;

Fat32 *g_fs = nullptr;

// --- Isolation test: thread_a and thread_b each kmmap() the identical VA
// (pagetable::kUserBase) in their own, separate address spaces, backed by
// different files, and each writes its own private sentinel there. Since
// only main() ever prints (console::print()'s own atomicity doesn't cover
// a multi-call message -- see console.hpp -- and these two run on
// genuinely different cores/threads), neither worker prints anything;
// they only set flags main() polls.

volatile bool g_a_wrote = false;
volatile bool g_b_wrote = false;
volatile bool g_a_done = false;
volatile bool g_b_done = false;

void thread_a() {
    File f = g_fs->find("/a.txt");
    if (!f.valid()) {
        test::fail("thread_a: a.txt not found");
    }
    if (!kmmap::kmmap(pagetable::kUserBase, 4096, f, 0, pagetable::PageFlags::kWrite)) {
        test::fail("thread_a: kmmap failed");
    }

    volatile char *p = reinterpret_cast<volatile char *>(pagetable::kUserBase);
    if (p[0] != 'A') {
        test::fail("thread_a: initial content wrong");
    }
    // Byte 100 is well past a.txt's 32-byte length, inside the same
    // 4KiB page -- must read as zero (standard private-mapping EOF
    // behavior), not leftover content from whatever this physical page
    // held before (e.g. another thread's freed page). a.txt/b.txt are
    // both short enough that checking only byte 0 above wouldn't catch
    // a missing zero-fill, since the file read always overwrites byte 0
    // regardless; this is the check that actually depends on it.
    if (p[100] != 0) {
        test::fail("thread_a: byte past EOF isn't zero -- leaked old page content?");
    }
    p[0] = 'X';   // private writes -- must never become visible to thread_b
    p[100] = 'Q';
    g_a_wrote = true;

    for (int i = 0; i < kSpinRounds && !g_b_wrote; i++) {
        threads::yield();
    }
    if (!g_b_wrote) {
        test::fail("thread_a: thread_b never wrote");
    }

    // thread_b has now written its own sentinels at the SAME virtual
    // address, in its OWN address space -- if the two weren't actually
    // isolated (e.g. a missing TLB flush serving a stale translation, or
    // both ending up pointed at the same physical page), these would now
    // read 'Y'/'Z' instead of 'X'/'Q'.
    if (p[0] != 'X' || p[100] != 'Q') {
        test::fail("thread_a: own write clobbered by another thread's mapping");
    }
    g_a_done = true;
}

void thread_b() {
    File f = g_fs->find("/b.txt");
    if (!f.valid()) {
        test::fail("thread_b: b.txt not found");
    }
    if (!kmmap::kmmap(pagetable::kUserBase, 4096, f, 0, pagetable::PageFlags::kWrite)) {
        test::fail("thread_b: kmmap failed");
    }

    volatile char *p = reinterpret_cast<volatile char *>(pagetable::kUserBase);
    if (p[0] != 'B') {
        test::fail("thread_b: initial content wrong");
    }
    if (p[100] != 0) {
        test::fail("thread_b: byte past EOF isn't zero -- leaked old page content?");
    }
    p[0] = 'Y';
    p[100] = 'Z';
    g_b_wrote = true;

    for (int i = 0; i < kSpinRounds && !g_a_wrote; i++) {
        threads::yield();
    }
    if (!g_a_wrote) {
        test::fail("thread_b: thread_a never wrote");
    }

    if (p[0] != 'Y' || p[100] != 'Z') {
        test::fail("thread_b: own write clobbered by another thread's mapping");
    }
    g_b_done = true;
}

// --- kmunmap() bookkeeping: runs directly in main()'s own thread/address
// space (no second thread needed), using multi.txt's three distinct
// pages ('a'/'b'/'c') so offset arithmetic after a shrink/split is
// actually checkable, not just "didn't crash".
void test_kmunmap_bookkeeping() {
    constexpr uint64_t kBase = pagetable::kUserBase;
    volatile char *p0 = reinterpret_cast<volatile char *>(kBase);
    volatile char *p1 = reinterpret_cast<volatile char *>(kBase + 4096);
    volatile char *p2 = reinterpret_cast<volatile char *>(kBase + 2 * 4096);

    File multi = g_fs->find("/multi.txt");
    File a = g_fs->find("/a.txt");
    File b = g_fs->find("/b.txt");
    if (!multi.valid() || !a.valid() || !b.valid()) {
        test::fail("kmunmap test: fixture files not found");
    }

    // Split: map all 3 pages, touch all 3, unmap exactly the middle one.
    if (!kmmap::kmmap(kBase, 3 * 4096, multi, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: 3-page kmmap failed");
    }
    if (p0[0] != 'a' || p1[0] != 'b' || p2[0] != 'c') {
        test::fail("kmunmap test: initial 3-page content wrong");
    }
    kmmap::kmunmap(kBase + 4096, 4096);
    if (p0[0] != 'a') {
        test::fail("kmunmap test: left piece disturbed by split");
    }
    if (p2[0] != 'c') {
        test::fail("kmunmap test: right piece disturbed by split");
    }
    // The freed middle VA must be kmmap-able again, and the fresh mapping
    // must show its OWN content -- not whatever the old page table leaf
    // still held, which would mean unmap_page() didn't actually clear it.
    if (!kmmap::kmmap(kBase + 4096, 4096, a, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: re-kmmap of split-freed VA failed");
    }
    if (p1[0] != 'A') {
        test::fail("kmunmap test: stale page table entry survived unmap");
    }
    kmmap::kmunmap(kBase, 3 * 4096); // clean slate -- covers all 3 surviving pieces at once

    // Right-shrink: 2-page mapping, unmap just the last page.
    if (!kmmap::kmmap(kBase, 2 * 4096, b, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: right-shrink setup kmmap failed");
    }
    if (p0[0] != 'B') {
        test::fail("kmunmap test: right-shrink initial content wrong");
    }
    kmmap::kmunmap(kBase + 4096, 4096);
    if (p0[0] != 'B') {
        test::fail("kmunmap test: right-shrink corrupted surviving piece");
    }
    if (!kmmap::kmmap(kBase + 4096, 4096, a, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: re-kmmap after right-shrink failed");
    }
    kmmap::kmunmap(kBase, 2 * 4096);

    // Left-shrink: 2-page mapping of multi.txt, NEITHER page touched yet
    // (nothing resident), unmap just the first page. The surviving
    // page's first-ever fault must read from the correctly-adjusted file
    // offset (4096, i.e. 'b') -- not offset 0 ('a'), which is exactly
    // what a buggy file_offset adjustment in kmunmap()'s shrink-from-the-
    // front case would produce. Using an already-resident page here
    // wouldn't prove this, since it would read correctly regardless.
    if (!kmmap::kmmap(kBase, 2 * 4096, multi, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: left-shrink setup kmmap failed");
    }
    kmmap::kmunmap(kBase, 4096);
    if (p1[0] != 'b') {
        test::fail("kmunmap test: left-shrink didn't adjust file_offset correctly");
    }
    kmmap::kmunmap(kBase + 4096, 4096);

    // Full delete, then a successful re-kmmap of the exact same VA.
    if (!kmmap::kmmap(kBase, 4096, a, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: single-page kmmap failed");
    }
    kmmap::kmunmap(kBase, 4096);
    if (!kmmap::kmmap(kBase, 4096, b, 0, pagetable::PageFlags::kWrite)) {
        test::fail("kmunmap test: re-kmmap after full delete failed");
    }
    kmmap::kmunmap(kBase, 4096);
}

} // namespace

extern "C" void main() {
    // Heap-allocated, never freed: Fat32 keeps a reference to `disk`, and
    // both must outlive every thread in this test that reads through
    // g_fs -- simplest to guarantee by never tearing either down.
    auto *disk = new DiskReader();
    g_fs = new Fat32(*disk);
    if (!g_fs->valid()) {
        test::fail("mount failed");
    }

    test_kmunmap_bookkeeping();

    threads::go(thread_a);
    threads::go(thread_b);

    for (int i = 0; i < kSpinRounds && !(g_a_done && g_b_done); i++) {
        threads::yield();
    }
    if (!(g_a_done && g_b_done)) {
        test::fail("workers never finished");
    }

    test::pass();
}
