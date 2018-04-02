
#include "stdafx.h"
#include "common.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "SampleRateConverterInterface.h"
#include "PcmStreamRenderer.h"

const size_t DATA_BUFFERS_MAX = 10;

namespace PcmSrtreamRenderer
{
    Implementation::Implementation(const std::string& dump_file)
        : m_dump_file(dump_file)
    {
        ;
    }

    Implementation::~Implementation()
    {
        if (m_render_thread.joinable())
            Stop();
    }

    bool
    Implementation::Init()
    {
        bool result = true;
        try
        {
            HRESULT                     hr = S_OK;

            common::ComUniquePtr<WAVEFORMATEX>  p_mix_format(nullptr, &CoTaskMemFree);
            REFERENCE_TIME              rtRequestedDuration = REFTIMES_PER_SEC * 2;

            if(!SampleRateConverter::create(m_converter))
                throw std::exception("Failed to create converter instance.");

            // check state
            if (m_state != STATE_NONE)
                throw std::exception("Invalid state.");

            // create multimedia device enumerator
            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
            if (FAILED(hr))
                throw std::exception("Failed to create MMDeviceEnumerator instance.");

            // retrieve the default audio endpoint for the specified data-flow direction and role.
            hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice);
            if (FAILED(hr)) // see https://msdn.microsoft.com/en-us/library/windows/desktop/dd370813(v=vs.85).aspx for eConsole
                throw std::exception("Failed to get default audio rendering endpoint.");

            // create AudioClient
            hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
            if (FAILED(hr))
                throw std::exception("Failed to activate default audio rendering endpoint.");

            // retrieve the stream format that the audio engine uses for its internal processing of shared-mode streams
            {
                WAVEFORMATEX* mix_format = nullptr;
                hr = m_pAudioClient->GetMixFormat(&mix_format);
                if (FAILED(hr))
                    throw std::exception("Failed to get mix format.");

                WAVEFORMATEXTENSIBLE* pext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format);
                pext->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

                p_mix_format = common::ComUniquePtr<WAVEFORMATEX>{ mix_format, &CoTaskMemFree };

                WAVEFORMATEX* closest_format = nullptr;
                hr = m_pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, mix_format, &closest_format);
                if (closest_format)
                    CoTaskMemFree(closest_format);

                if (FAILED(hr))
                    throw std::exception("Failed to check is proposed format supported.");
            }

            // apply the rendering format
            hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, rtRequestedDuration, 0, p_mix_format.get(), NULL);
            if (FAILED(hr))
                throw std::exception("Failed to initialize default audio rendering endpoint.");

            // keep the rendering format
            m_format_render.reset(new PCMFormat{ p_mix_format->nSamplesPerSec, p_mix_format->nChannels, p_mix_format->wBitsPerSample, p_mix_format->nBlockAlign });
            
            //
            bool res = m_converter->SetOutputFormat(m_format_render);
            assert(res);

            // Get the actual size of the allocated buffer.
            hr = m_pAudioClient->GetBufferSize(&m_rendering_buffer_frames_total);
            if (FAILED(hr))
                throw std::exception("Failed to get buffer size.");

            // retireve the renderer client pointer
            hr = m_pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_pRenderClient);
            if (FAILED(hr))
                throw std::exception("Failed to get rendering service.");

            // calculate rendering buffer total duration
            m_rendering_buffer_duration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * m_rendering_buffer_frames_total) / m_format_render->samplesPerSecond;

            // update state - we are ready to consume data
            m_state = STATE_INITIAL;
        }
        catch (std::exception e)
        {
            result = false;
        }

        return result;
    }

    bool
    Implementation::SetFormat(const PCMFormat& format)
    {
        // check state
        assert(m_state == STATE_INITIAL);
        if (m_state != STATE_INITIAL)
            return false;

        std::shared_ptr<const PCMFormat> f(new PCMFormat(format));

        m_converter->SetInputFormat(f);

        //
        if (!m_converter->GetInputDataPort(m_converterInputPort))
            throw std::exception("Failed to obtain input data flow.");

        //
        if (!m_converter->GetOutputDataPort(m_converterOutputPort))
            throw std::exception("Failed to obtain output data flow.");

        m_state = STATE_STOPPED;

        Start();

        return true;
    }

    bool
    Implementation::GetFormat(PCMFormat& format) const
    {
        if (!m_converter)
            return false;
        
        std::shared_ptr<const PCMFormat> f;

        m_converter->GetInputFormat(f);

        format = *f;

        return true;
    }

    bool
    Implementation::Start()
    {
        HRESULT hr = S_OK;

        // check state
        assert(m_state == STATE_STOPPED);
        if (m_state != STATE_STOPPED)
            return false;

        // run rendering worker thread
        std::unique_lock<std::mutex> l(m_thread_running_mtx);
        m_render_thread = std::thread(std::bind(&Implementation::DoRender, this));
        m_thread_running_cv.wait(l);

        // update state
        m_state = STATE_STARTED;

        return true;
    }

    bool
    Implementation::Stop()
    {
        HRESULT hr = S_OK;

        if (m_state == STATE_STOPPED)
            return true;

        // check state
        assert(m_state == STATE_STARTED);
        if (m_state != STATE_STARTED)
            return false;

        // once rendering thread has been run
        if (m_render_thread.joinable())
        {
            m_thread_interraption.activate();

            m_render_thread.join();
        }

        // update state
        m_state = STATE_STOPPED;

        return true;
    }

    bool
    Implementation::WaitForCompletion()
    {
        if(!m_render_thread.joinable())
        {
            m_render_thread.join();
            return true;
        }
        
        bool r = m_thread_completor.wait();

        m_render_thread.join();

        return r;
    }

    Implementation::state
    Implementation::GetState() const
    {
        return m_state;
    }

    bool
    Implementation::PutBuffer(PCMDataBuffer::wptr& buffer)
    {
        if (m_state < STATE_STOPPED)
            return false;

        // put filled buffer to the data queue
        if (m_converterInputPort.expired())
            return false;

        return m_converterInputPort.lock()->PutBuffer(std::move(buffer));
    }

    bool
    Implementation::GetBuffer(PCMDataBuffer::wptr& buffer)
    {
        if (m_state < STATE_STOPPED)
            return false;

        if (m_converterInputPort.expired())
            return false;

        return m_converterInputPort.lock()->GetBuffer(buffer);
    }

    HRESULT 
    Implementation::FillBuffer(uint8_t * const buffer, const std::streamsize buffer_frames, std::streamsize& buffer_actual_frames, PCMDataBuffer::wptr& rendering_partially_processed_buffer)
    {
        std::streamsize buffer_frames_rest = buffer_frames;
        
        buffer_actual_frames = 0;
        bool end_of_stream = false;

        while (true)
        {
            std::shared_ptr<PCMDataBuffer> sbuffer;

            // chose buffer
            if (!rendering_partially_processed_buffer.expired())
            {// take the partially processed one
                sbuffer = rendering_partially_processed_buffer.lock();
                rendering_partially_processed_buffer.reset();
            }
            else
            {// or brand new one
                std::weak_ptr<PCMDataBuffer> wbuffer;

                // take buffer
                if (!InternalGetBuffer(wbuffer))
                    return E_ABORT;

                // check buffer
                if (wbuffer.expired())
                    throw std::exception("Source buffer has been expired.");

                // get actual buffer
                sbuffer = wbuffer.lock();
            }

            end_of_stream = sbuffer->end_of_stream;

            // offset in the rendering buffer
            const std::streamsize offset_frames = buffer_frames - buffer_frames_rest;

            // bytes to copy from the source buffer
            const std::streamsize frames_in_buffer = bytes_to_frames(sbuffer->actual_size);
            const std::streamsize frames_to_render = buffer_frames_rest < frames_in_buffer ? buffer_frames_rest : frames_in_buffer;

            // copy bytes_to_render bytes from source buffer to rendering buffer
            const std::streamsize offset_bytes = frames_to_bytes(offset_frames);
            const std::streamsize bytes_to_render = frames_to_bytes(frames_to_render);

            // copy data
            memcpy(buffer + offset_bytes, sbuffer->p.get(), bytes_to_render);

            // reduce the rest of rendering buffer with copied data 
            buffer_frames_rest -= frames_to_render;

            // once hte buffer had processed partially - keep it for the next session
            if (frames_to_render < frames_in_buffer)
            {
                // how many bytes to keep in the source buffer
                sbuffer->actual_size -= frames_to_bytes(frames_to_render);

                // move kept bytes to the source buffer beginning
                memmove(sbuffer->p.get(), ((char*)sbuffer->p.get() + frames_to_bytes(frames_to_render)), sbuffer->actual_size);

                // keep the buffer separately
                rendering_partially_processed_buffer = sbuffer;

                // prevent partially processed buffer from reaching free buffers queue
                sbuffer.reset();
            }

            if (sbuffer)
            {
                // return buffer to queue
                std::weak_ptr<PCMDataBuffer> wbuffer(sbuffer);
                if (!InternalPutBuffer(wbuffer))
                    throw std::exception("Failed to put data buffer back.");
            }

            // break the buffer filling
            if (end_of_stream || 0 == buffer_frames_rest)
                break;
        }

        buffer_actual_frames = buffer_frames - buffer_frames_rest;
        if (buffer_actual_frames < buffer_frames)
        {
            assert(true == end_of_stream);
            return S_FALSE;
        }

        return S_OK;
    }

    HRESULT
    Implementation::DoRender()
    {
        {
            std::unique_lock<std::mutex> l(m_thread_running_mtx);
            m_thread_running_cv.notify_all();
        }

        HRESULT hr = S_OK;

#ifdef _DEBUG
        std::ofstream out_file;
        if(0 < m_dump_file.length())
            out_file.open(m_dump_file, std::ios_base::out | std::ios_base::binary);
#endif

        bool                rendering_started                   = false;

        PBYTE               rendering_buffer                    = NULL;

        std::streamsize     rendering_buffer_frames_avaliable   = 0;
        std::streamsize     rendering_buffer_frames_actual      = 0;

        PCMDataBuffer::wptr rendering_partially_processed_buffer;

        // render loop
        try
        {
            while (true)
            {
                if (m_thread_interraption.wait())
                    break;

                // take the rendering buffer
                UINT32 rendering_buffer_frames_padding = 0;
                if (rendering_started)
                {
                    // see how much buffer space is available.
                    hr = m_pAudioClient->GetCurrentPadding(&rendering_buffer_frames_padding);
                    assert(S_OK == hr);
                    if (FAILED(hr))
                        throw std::exception("Failed to get current padding");
                }

                // calc avaliable buffer frames
                rendering_buffer_frames_avaliable = m_rendering_buffer_frames_total - rendering_buffer_frames_padding;

                // grab the avaliable buffer
                hr = m_pRenderClient->GetBuffer((UINT32)rendering_buffer_frames_avaliable, &rendering_buffer);
                if (FAILED(hr))
                    throw std::exception("Failed to get rendering buffer.");

                // fill device buffer with data
                do
                {
                    if(E_ABORT == (hr = FillBuffer(rendering_buffer, rendering_buffer_frames_avaliable, rendering_buffer_frames_actual, rendering_partially_processed_buffer)))
                        continue;

                    if (FAILED(hr))
                        throw std::exception("Failed to fill buffer.");
                    else if (SUCCEEDED(hr))
                        break;
                } while (!m_thread_interraption.wait());

                // finally fulfilled the rendering buffer
                if (SUCCEEDED(hr))
                {
                    bool leave = (S_FALSE == hr);
                    
                    // let the device to render buffer
                    hr = m_pRenderClient->ReleaseBuffer((UINT32)rendering_buffer_frames_actual, 0);
                    assert(S_OK == hr);
                    if (FAILED(hr))
                        throw std::exception("Failed to release rendering buffer.");

#ifdef _DEBUG
                    // bump data
                    if(out_file.is_open())
                        out_file.write((char*)rendering_buffer, rendering_buffer_frames_actual * m_format_render->bytesPerFrame);
#endif

                    // launch rendering device if not
                    if (!rendering_started)
                    {
                        // start playing.
                        hr = m_pAudioClient->Start();
                        assert(S_OK == hr);
                        if (FAILED(hr))
                            throw std::exception("Failure while calling to IAudioClient::Start.");

                        rendering_started = true;
                    }

                    // sleep for half the buffer duration.
                    Sleep((DWORD)(m_rendering_buffer_duration / REFTIMES_PER_MILLISEC / 4));

                    // once the buffer fillled partially this means no more data available
                    if (leave)
                        break;
                }
            }
        }
        catch (std::exception exc)
        {
            hr = E_FAIL;
        }

#ifdef _DEBUG        
        if (out_file.is_open())
            out_file.close();
#endif

        if (m_thread_completor)
            m_thread_completor.complete();
        else
            m_thread_interraption.wait(std::chrono::milliseconds(500));

        return hr;
    }

    bool
    Implementation::InternalPutBuffer(std::weak_ptr<PCMDataBuffer>& buffer)
    {
        if (m_converterOutputPort.expired())
            return false;

        return m_converterOutputPort.lock()->PutBuffer(buffer);
    }

    bool
    Implementation::InternalGetBuffer(std::weak_ptr<PCMDataBuffer>& buffer)
    {
        if (m_converterOutputPort.expired())
            return false;

        return m_converterOutputPort.lock()->GetBuffer(buffer);
    }
}
