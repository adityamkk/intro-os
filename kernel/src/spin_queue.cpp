#include "spin_queue.hpp"

SpinQueue::~SpinQueue() {
    void *value;
    while (pop(&value)) {
    }
}

void SpinQueue::push(void *value) {
    // Allocate outside the critical section -- no need to hold lock_
    // while the heap does its own (separate) locking.
    Node *node = new Node{value, nullptr};

    lock_.lock();
    if (tail_ == nullptr) {
        head_ = tail_ = node;
    } else {
        tail_->next = node;
        tail_ = node;
    }
    lock_.unlock();
}

bool SpinQueue::pop(void **out) {
    lock_.lock();
    Node *node = head_;
    if (node != nullptr) {
        head_ = node->next;
        if (head_ == nullptr) {
            tail_ = nullptr;
        }
    }
    lock_.unlock();

    if (node == nullptr) {
        return false;
    }

    *out = node->value;
    delete node;
    return true;
}

bool SpinQueue::empty() const {
    lock_.lock();
    bool is_empty = head_ == nullptr;
    lock_.unlock();
    return is_empty;
}
