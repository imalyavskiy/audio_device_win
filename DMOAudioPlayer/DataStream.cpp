#include "stdafx.h"
#include "common.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "DataStream.h"

void DataStream::DoStream()
{
    std::weak_ptr<PCMDataBuffer> wbuffer;

    {
        std::unique_lock<std::mutex> l(m_stream_thread_mtx);
        m_stream_thread_cv.notify_all();
    }

    do
    {
        do {
            if (!m_renderer->GetBuffer(wbuffer))
                continue;
            else
                break;
        } while (!m_interraptor.wait());

        if (wbuffer.expired())
            break;

        std::shared_ptr<PCMDataBuffer> sbuffer(wbuffer.lock());
        if (!m_source->ReadData(sbuffer))
            break;

        if (!m_renderer->PutBuffer(wbuffer))
            break;

    } while (!m_interraptor.wait());

    if (m_completor)
    {
        m_renderer->WaitForCompletion();
        m_completor.complete();
    }
    else
    {
        m_interraptor.wait(std::chrono::milliseconds(500));
    }

    return;
}

DataStream::DataStream(WavAudioSource::Interface::ptr source, PcmSrtreamRenderer::Interface::ptr renderer) 
    : m_renderer(renderer)
    , m_source(source)
{
    ;
}

DataStream::~DataStream()
{
    Stop();
}

bool DataStream::Init()
{
    PCMFormat file_PCM_format{ 0,0,0,0 };

    if (!m_source->GetFormat(file_PCM_format))
        return false;

    if (!m_renderer->SetFormat(file_PCM_format, file_PCM_format.samplesPerSecond / 10, 10))
        return false;

    return true;
}

bool DataStream::Start()
{
    std::unique_lock<std::mutex> l(m_stream_thread_mtx);
    m_stream_thread = std::thread(std::bind(&DataStream::DoStream, this));
    m_stream_thread_cv.wait(l);

    return m_stream_thread.joinable();
}

bool DataStream::Stop()
{
    m_interraptor.activate();

    if (m_stream_thread.joinable())
        m_stream_thread.join();

    m_renderer->Stop();

    return true;
}

bool DataStream::WaitForCompletion()
{
    if (!m_stream_thread.joinable())
    {
        m_stream_thread.join();
        return true;
    }

    bool r = m_completor.wait();

    m_stream_thread.join();

    return r;
}
