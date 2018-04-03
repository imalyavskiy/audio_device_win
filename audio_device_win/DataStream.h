#ifndef __DATA_STREAM_H__
#define __DATA_STREAM_H__
#pragma once

class DataStream
{
    void DoStream();

public:
    DataStream(IWavAudioSource::ptr source, ISampleRateConverter::ptr converter, IPcmSrtreamRenderer::ptr renderer);

    ~DataStream();

    bool Init();

    bool Start();

    bool Stop();

    bool WaitForCompletion();

protected:
    std::mutex                          m_stream_thread_mtx;
    std::condition_variable             m_stream_thread_cv;

    std::thread                         m_stream_thread;

    IWavAudioSource::ptr                m_source;
    
    ISampleRateConverter::ptr           m_converter;

    IPcmSrtreamRenderer::ptr            m_renderer;

    common::ThreadInterraptor           m_interraptor;

    common::ThreadCompletor             m_completor;
};


#endif // __DATA_STREAM_H__
