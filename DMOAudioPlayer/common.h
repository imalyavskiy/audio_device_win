#ifndef __COMMON_H__
#define __COMMON_H__
#pragma once

namespace common
{
    template<class T> using ComUniquePtr = std::unique_ptr<T, decltype(&CoTaskMemFree)>;

    template<typename T>
    class BufferQueue
    {
    public:
        typedef std::chrono::steady_clock::time_point time_point;
        typedef std::chrono::steady_clock clock;
        typedef std::chrono::steady_clock::duration duration;
        typedef std::chrono::milliseconds milliseconds;

        bool get(T& t)
        {
            duration time_to_wait(milliseconds(500));

            std::unique_lock<decltype(m)> l(m);

            while (q.empty()) // Handle spurious wake-ups.
            {
                const time_point before = clock::now();

                if(std::cv_status::no_timeout == cv.wait_for(l, time_to_wait) && q.empty())
                {
                    time_to_wait -= clock::now() - before;
                    continue;
                }
                
                break;
            }

            if (q.empty())
                return false;

            // take buffer
            t = q.front();
            q.pop();

            return true;
        }

        bool put(T t)
        {
            std::unique_lock<decltype(m)> l(m);

            q.push(t); // put buffer

            cv.notify_one();

            return true;
        }

    private:
        std::queue<T> q;

        std::mutex m;
        std::condition_variable cv;
    };

    class ThreadInterraptor
    {
    public:
        ThreadInterraptor()
        {
            ;
        }

        ~ThreadInterraptor()
        {
            ;
        }

        bool wait(std::chrono::milliseconds timeout = std::chrono::milliseconds(0))
        {
            std::unique_lock<std::mutex> l(mtx);
            bool result = std::cv_status::no_timeout == cv.wait_for(l, timeout);
            return result;
        }

        void activate()
        {
            std::unique_lock<std::mutex> l(mtx);
            cv.notify_all();
        }

        void reset()
        {
            std::unique_lock<std::mutex> l(mtx);
            active = false;
        }

    protected:
        bool        active = false;
        std::mutex  mtx;
        std::condition_variable cv;
    };

    class ThreadCompletor
    {
    public:
        ThreadCompletor()
        {
            ;
        }

        ~ThreadCompletor()
        {
            ;
        }
        
        bool wait()
        {
            std::unique_lock<std::mutex> l(mtx);
            active = true;
            cv.wait(l);
            return true;
        }

        void complete()
        {
            std::unique_lock<std::mutex> l(mtx);
            active = false;
            cv.notify_all();
        }

        operator bool() {
            std::unique_lock<std::mutex> l(mtx);
            return active;
        }

    protected:
        std::mutex mtx;
        std::condition_variable cv;
        
        bool active = false;
    };
}

#endif // __COMMON_H__
