#pragma once

// sample of the semaphore implemented by use of mutex and conditional variable
class semaphore
{
private:
    std::mutex m;
    std::condition_variable cv;
    unsigned long c = 0; // Initialized as locked.

public:
    void notify() {
        std::unique_lock<decltype(m)> l(m);
        ++c;
        cv.notify_one();
    }

    void wait() {
        std::unique_lock<decltype(m)> l(m);
        while (!c) // Handle spurious wake-ups.
            cv.wait(l);
        --c;
    }

    bool try_wait() {
        std::unique_lock<decltype(m)> l(m);
        if (c) {
            --c;
            return true;
        }
        return false;
    }
};