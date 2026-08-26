#pragma once

#include "spinlock.hpp"

// A FIFO queue of `void *` elements backed by a singly linked list, guarded
// by a Spinlock so multiple cores can push()/pop() concurrently. Elements
// are opaque to the queue -- callers own whatever they point at and are
// responsible for casting back to the real type.
//
// Nodes are heap-allocated (see heap.hpp) on push() and freed on pop(), so
// callers must not use a SpinQueue before heap::init() has run.
class SpinQueue {
public:
    SpinQueue() = default;

    // Drains any still-queued nodes so they aren't leaked; does not touch
    // whatever the caller's values point at.
    ~SpinQueue();

    // Copying would let two SpinQueues diverge on what should be one
    // shared list, and moving would leave dangling next/tail pointers
    // mid-list -- neither is supported.
    SpinQueue(const SpinQueue &) = delete;
    SpinQueue &operator=(const SpinQueue &) = delete;

    // Adds `value` to the back of the queue. `value` may be nullptr --
    // the queue doesn't inspect it, only stores and returns it.
    void push(void *value);

    // If the queue is non-empty, removes the value at its front into
    // `*out` and returns true. Otherwise returns false and leaves `*out`
    // untouched. (A bool return, rather than pop() returning the value
    // directly with nullptr meaning "empty", is what lets nullptr be a
    // valid element to push() in the first place.)
    bool pop(void **out);

    // A snapshot, immediately stale in the presence of concurrent
    // pushers/poppers -- same caveat as any concurrent queue's empty().
    bool empty() const;

private:
    struct Node {
        void *value;
        Node *next;
    };

    mutable Spinlock lock_;
    Node *head_ = nullptr;
    Node *tail_ = nullptr;
};
