#ifndef __COMMON_H__
#define __COMMON_H__
#pragma once

namespace common
{
    template<class T> using ComUniquePtr = std::unique_ptr<T, decltype(&CoTaskMemFree)>;

    struct DataPortInterface
    {
        typedef std::weak_ptr<DataPortInterface> wptr;

        virtual bool GetBuffer(PCMDataBuffer::wptr& p) = 0;
        virtual bool PutBuffer(PCMDataBuffer::wptr p) = 0;
    };

    class BufferQueue
    {
    public:
        typedef std::chrono::steady_clock::time_point time_point;
        typedef std::chrono::steady_clock clock;
        typedef std::chrono::steady_clock::duration duration;
        typedef std::chrono::milliseconds milliseconds;

        bool GetBuffer(PCMDataBuffer::wptr& t)
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

        bool PutBuffer(PCMDataBuffer::wptr t)
        {
            std::unique_lock<decltype(m)> l(m);

            q.push(t); // put buffer

            cv.notify_one();

            return true;
        }

    private:
        std::queue<PCMDataBuffer::wptr> q;

        std::mutex m;
        std::condition_variable cv;
    };

    class DataPort
        : public DataPortInterface
    {
    public:
        DataPort(std::shared_ptr<common::BufferQueue>& inQueue, std::shared_ptr<common::BufferQueue>& outQueue)
            : m_inQueue(inQueue)
            , m_outQueue(outQueue)
        {
            ;
        }

        bool GetBuffer(PCMDataBuffer::wptr& p) override
        {
            if (!m_inQueue || !m_outQueue)
                return false;

            return m_inQueue->GetBuffer(p);
        }

        bool PutBuffer(PCMDataBuffer::wptr p) override
        {
            if (!m_inQueue || !m_outQueue)
                return false;

            m_outQueue->PutBuffer(p);

            return true;
        }

    protected:
        std::shared_ptr<common::BufferQueue>         m_inQueue;
        std::shared_ptr<common::BufferQueue>         m_outQueue;
    };

    class DataFlow
    {
    public:

        ~DataFlow()
        {
            ;
        }

        bool Alloc(const size_t bytes_per_buffer, const size_t buffers)
        {
            m_busyBufferQueue.reset(new common::BufferQueue);
            m_freeBufferQueue.reset(new common::BufferQueue);

            for (size_t c = 0; c < buffers; ++c)
            {
                m_bufferStorage.push_back(PCMDataBuffer::sptr(new PCMDataBuffer(new int8_t[bytes_per_buffer]{ 0 }, bytes_per_buffer)));
                m_freeBufferQueue->PutBuffer(m_bufferStorage.back());
            }
            

            m_iPort.reset(new DataPort(/*get*/m_freeBufferQueue, /*put*/m_busyBufferQueue));
            m_oPort.reset(new DataPort(/*get*/m_busyBufferQueue, /*put*/m_freeBufferQueue));

            return true;
        }

        bool inputPort(DataPortInterface::wptr& port)
        {
            if (!m_iPort || !m_oPort)
                return false;

            port = m_iPort;

            return true;
        }

        bool outputPort(DataPortInterface::wptr& port)
        {
            if (!m_iPort || !m_oPort)
                return false;

            port = m_oPort;

            return true;
        }

    protected:
        std::shared_ptr<DataPortInterface>   m_iPort;
        std::shared_ptr<DataPortInterface>   m_oPort;

        std::shared_ptr<common::BufferQueue> m_busyBufferQueue;
        std::shared_ptr<common::BufferQueue> m_freeBufferQueue;
        std::list<PCMDataBuffer::sptr>       m_bufferStorage;
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
