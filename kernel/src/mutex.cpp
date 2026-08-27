#include "mutex.hpp"

Mutex::Mutex() : sem(1) {
    // Initialize the semaphore to size 1
}

void Mutex::lock() {
    sem.down();
}

void Mutex::unlock() {
    sem.up();
}