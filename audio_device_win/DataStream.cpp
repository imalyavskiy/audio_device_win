#include "stdafx.h"
#include "common.h"
#include "AudioSourceInterface.h"
#include "SampleRateConverterInterface.h"
#include "PcmStreamRendererInterface.h"
#include "DataStream.h"

void DataStream::DoStream()
{
    std::weak_ptr<PCMDataBuffer> wbuffer;

    {
        std::unique_lock<std::mutex> l(m_stream_thread_mtx);
        m_stream_thread_cv.notify_all();
    }
    
    common::DataPortInterface::wptr converter_in;
    if (!m_converter->GetInputDataPort(converter_in))
        assert(false);

    do
    {
        do {
            if (!converter_in.lock()->GetBuffer(wbuffer))
                continue;
            else
                break;
        } while (!m_interraptor.wait());

        if (wbuffer.expired())
            break;

        std::shared_ptr<PCMDataBuffer> sbuffer(wbuffer.lock());
        if (!m_source->ReadData(sbuffer))
            break;

        if (!converter_in.lock()->PutBuffer(wbuffer))
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

DataStream::DataStream(IWavAudioSource::ptr source, ISampleRateConverter::ptr converter, IPcmSrtreamRenderer::ptr renderer)
    : m_renderer(renderer)
    , m_converter(converter)
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
    std::shared_ptr<PCMFormat> src_PCM_format(new PCMFormat{ PCMFormat::uns, 0, 0, 0, 0 });
    std::shared_ptr<PCMFormat> dst_PCM_format(new PCMFormat{ PCMFormat::uns, 0, 0, 0, 0 });

    common::DataPortInterface::wptr converter_out;

    if (!m_source->GetFormat(*src_PCM_format))
        return false;

    if (!m_renderer->GetFormat(*dst_PCM_format))
        return false;

    if (!m_converter->SetFormats(src_PCM_format, dst_PCM_format))
        return false;

    if (!m_converter->GetOutputDataPort(converter_out))
        return false;

    if(!m_renderer->SetDataPort(converter_out))
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
