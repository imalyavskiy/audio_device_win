
#include "stdafx.h"
#include "common.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "PcmStreamRenderer.h"

const size_t DATA_BUFFERS_MAX = 100;
namespace PcmSrtreamRenderer
{
    Implementation::Implementation(const std::string& dump_file)
        : m_hRenderThread(NULL)
        , m_dwThreadId(0)
        , m_hRenderThreadExitEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
        , m_hNewDataBufferSemaphore(CreateSemaphore(NULL, 0, DATA_BUFFERS_MAX, NULL))
        , m_hFreeDataBuffersSemaphore(CreateSemaphore(NULL, 0, DATA_BUFFERS_MAX, NULL))
        , m_dump_file(dump_file)
    {
        assert(m_hNewDataBufferSemaphore != NULL && m_hNewDataBufferSemaphore != INVALID_HANDLE_VALUE);
        assert(m_hRenderThreadExitEvent != NULL && m_hRenderThreadExitEvent != INVALID_HANDLE_VALUE);
    }

    Implementation::~Implementation()
    {
        ;
    }

    bool
    Implementation::Init()
    {
        HRESULT                     hr = S_OK;
        ComUniquePtr<WAVEFORMATEX>  p_mix_format(nullptr, nullptr);
        REFERENCE_TIME              rtRequestedDuration = REFTIMES_PER_SEC * 2;

        // check state
        assert(m_state == STATE_NONE);
        if (m_state != STATE_NONE)
            return false;

        // create multimedia device enumerator
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
        assert(S_OK == hr);
        if (FAILED(hr))
            return SUCCEEDED(hr);

        // retrieve the default audio endpoint for the specified data-flow direction and role.
        hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice);
        assert(S_OK == hr);
        if (FAILED(hr)) // see https://msdn.microsoft.com/en-us/library/windows/desktop/dd370813(v=vs.85).aspx for eConsole
            return SUCCEEDED(hr);

        // create AudioClient
        hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
        assert(S_OK == hr);
        if (FAILED(hr))
            return SUCCEEDED(hr);

        // retrieve the stream format that the audio engine uses for its internal processing of shared-mode streams
        {
            WAVEFORMATEX* mix_format = nullptr;
            hr = m_pAudioClient->GetMixFormat(&mix_format);
            assert(S_OK == hr);
            if (FAILED(hr))
                return SUCCEEDED(hr);

            WAVEFORMATEXTENSIBLE* pext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format);

            p_mix_format = ComUniquePtr<WAVEFORMATEX>{ mix_format, &CoTaskMemFree};

            WAVEFORMATEX* closest_format = nullptr;
            hr = m_pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, mix_format, &closest_format);
            assert(S_OK == hr);
            if (closest_format)
                CoTaskMemFree(closest_format);
            
            if (FAILED(hr))
                return SUCCEEDED(hr);
        }

        // apply the rendering format
        hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, rtRequestedDuration, 0, p_mix_format.get(), NULL);
        assert(S_OK == hr);
        if (FAILED(hr))
            return SUCCEEDED(hr);

        // keep the rendering format
        m_format_render.reset(new PCMFormat{ p_mix_format->nSamplesPerSec, p_mix_format->nChannels, p_mix_format->wBitsPerSample, p_mix_format->nBlockAlign });

        // Get the actual size of the allocated buffer.
        hr = m_pAudioClient->GetBufferSize(&m_rendering_buffer_frames_total);
        assert(S_OK == hr);
        if (FAILED(hr))
            return hr;

        // retireve the renderer client pointer
        hr = m_pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_pRenderClient);
        assert(S_OK == hr);
        if (FAILED(hr))
            return hr;

        // retrieve volume control interface
        hr = m_pAudioClient->GetService(__uuidof(ISimpleAudioVolume), (void**)&m_pVolumeControl);
        assert(S_OK == hr);
        if (FAILED(hr))
            return hr;

        // get volume level
        float level = .0;
        hr = m_pVolumeControl->GetMasterVolume(&level);
        assert(S_OK == hr);
        if (FAILED(hr))
            return hr;

        // calculate rendering buffer total duration
        m_rendering_buffer_duration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * m_rendering_buffer_frames_total) / m_format_render->samplesPerSecond;

#ifdef _DEBUG
        // retrieve stream latency
        REFERENCE_TIME rtStreamLatency = 0;
        hr = m_pAudioClient->GetStreamLatency(&rtStreamLatency);
        assert(S_OK == hr);
        if (FAILED(hr))
            return hr;
#endif
        // update state - we are ready to consume data
        m_state = STATE_INITIAL;

        return true;
    }

    bool
    Implementation::SetFormat(const PCMFormat& format, const size_t buffer_frames, const size_t buffers_total)
    {
        // check state
        assert(m_state == STATE_INITIAL);
        if (m_state != STATE_INITIAL)
            return false;

        // keep format input format data
        m_format_in.reset(new PCMFormat(format));
        m_buffer_frames.reset(new size_t(buffer_frames));
        m_buffers_total.reset(new size_t(buffers_total));

        // update state
        m_state = STATE_STOPPED;

        // calculate single buffer size in bytes
        const size_t buffer_size = m_format_in->bytesPerFrame * buffer_frames;

        // allocate and put buffers to the appropriate location
        for(size_t cBuffer = 0; cBuffer < buffers_total; ++cBuffer)
        {
            PCMDataBuffer::sptr pB(new PCMDataBuffer{ new uint8_t[buffer_size], (const uint32_t)buffer_size, 0 , false });
            
            m_bufferStorage.push_back(pB);

            m_freeBufffersQueue.push(pB);

            // update semaphore to reflect correct bugffers count in the queue
            ReleaseSemaphore(m_hFreeDataBuffersSemaphore, 1, nullptr);
        }

        return true;
    }

    bool
    Implementation::GetFormat(PCMFormat& format, size_t& buffer_frames, size_t& buffers_total) const
    {
        if (m_format_in)
            return false;

        memcpy(&format, m_format_in.get(), sizeof(PCMFormat));

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
        m_hRenderThread = CreateThread(NULL, 0, &Implementation::DoRenderThread, LPVOID(this), 0, &m_dwThreadId);

        // update state
        m_state = STATE_STARTED;

        return true;
    }

    bool
    Implementation::Stop()
    {
        HRESULT hr = S_OK;

        // check state
        assert(m_state == STATE_STARTED);
        if (m_state != STATE_STARTED)
            return false;

        // once rendering thread has been run
        if (m_hRenderThread)
        {
            BOOL bResult = FALSE;

            // notify it to exit
            bResult = SetEvent(m_hRenderThreadExitEvent);
            assert(TRUE == bResult);

            // wait 10 seconds until thread will exit
            if (WAIT_TIMEOUT == WaitForSingleObject(m_hRenderThread, 60000))
            {
                // finally close it
                CloseHandle(m_hRenderThread);
                m_hRenderThread = NULL;
            }

        }

        // update state
        m_state = STATE_STOPPED;

        return true;
    }

    Implementation::state
    Implementation::GetState() const
    {
        return m_state;
    }

    bool
    Implementation::PutBuffer(std::weak_ptr<PCMDataBuffer>& buffer)
    {
        if (m_state < STATE_STOPPED)
            return false;

        AutoLock l(m_cs);
        {
            // but filled buffer to the data queue
            m_inputDataQueue.push(std::move(buffer));
            buffer.reset();
            
            // notify render thread about new data arrived
            BOOL result = ReleaseSemaphore(m_hNewDataBufferSemaphore, 1, NULL);
            assert(result == TRUE);
        }

        // if we did not run before - run 
        if (m_state == STATE_STOPPED)
            Start();

        return true;
    }

    bool
    Implementation::GetBuffer(std::weak_ptr<PCMDataBuffer>& buffer)
    {
        HANDLE wait_objects[]{ m_hRenderThreadExitEvent, m_hFreeDataBuffersSemaphore };

        // wait for free buffer to appear or for the cancellation event
        DWORD dwResult = WaitForMultipleObjects(2, wait_objects, FALSE, INFINITE);
        if (dwResult != (WAIT_OBJECT_0 + 1))
            return false;

        {
            AutoLock l(m_cs);

            // get free buffer
            buffer = m_freeBufffersQueue.front();
            m_freeBufffersQueue.pop();
        }

        return true;
    }

    DWORD WINAPI
    Implementation::DoRenderThread(LPVOID param)
    {
        HRESULT hr = E_FAIL;
        
        std::cout << "Render thread started." << std::endl;

        if (param != NULL)
            // call the member thread function 
            hr = ((Implementation*)param)->DoRender();

        std::cout << "Render thread stopped." << std::endl;

        return hr;
    }
    HRESULT 
    Implementation::FillBuffer(BYTE * const buffer, const UINT32 buffer_frames, UINT32& buffer_actual_frames, PCMDataBuffer::wptr& rendering_partially_processed_buffer)
    {
        UINT32 buffer_frames_rest = buffer_frames;
        
        buffer_actual_frames = 0;
        bool last_buffer = false;

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
                    throw std::exception("Failed to get sourse buffer");

                // check buffer
                if (wbuffer.expired())
                    throw std::exception("Source buffer has been expired.");

                // get actual buffer
                sbuffer = wbuffer.lock();
            }

            last_buffer = sbuffer->last;

            // offset in the rendering buffer
            const uint32_t offset_frames = buffer_frames - buffer_frames_rest;

            // bytes to copy from the source buffer
            const uint32_t frames_in_buffer = bytes_to_frames(sbuffer->asize);
            const uint32_t frames_to_render = buffer_frames_rest < frames_in_buffer ? buffer_frames_rest : frames_in_buffer;

            // copy bytes_to_render bytes from source buffer to rendering buffer
            const size_t offset_bytes = frames_to_bytes(offset_frames);
            const size_t bytes_to_render = frames_to_bytes(frames_to_render);

            // copy data
            memcpy(buffer + offset_bytes, sbuffer->p, bytes_to_render);

            // reduce the rest of rendering buffer with copied data 
            buffer_frames_rest -= frames_to_render;

            // once hte buffer had processed partially - keep it for the next session
            if (frames_to_render < frames_in_buffer)
            {
                // how many bytes to keep in the source buffer
                sbuffer->asize -= frames_to_bytes(frames_to_render);

                // move kept bytes to the source buffer beginning
                memmove(sbuffer->p, ((char*)sbuffer->p + frames_to_bytes(frames_to_render)), sbuffer->asize);

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
            if (last_buffer || 0 == buffer_frames_rest)
                break;
        }

        buffer_actual_frames = buffer_frames - buffer_frames_rest;
        if (buffer_actual_frames < buffer_frames)
            return S_FALSE;

        return S_OK;
    }

    HRESULT
    Implementation::DoRender()
    {
        HRESULT hr = S_OK;
        std::ofstream out_file;
        if(0 < m_dump_file.length())
            out_file.open(m_dump_file, std::ios_base::out | std::ios_base::binary);

        bool                rendering_started                   = false;

        PBYTE               rendering_buffer                    = NULL;

        UINT32              rendering_buffer_frames_avaliable   = 0;
        UINT32              rendering_buffer_frames_actual      = 0;

        PCMDataBuffer::wptr rendering_partially_processed_buffer;

        // render loop
        try{
            while (true)
            {
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
                hr = m_pRenderClient->GetBuffer(rendering_buffer_frames_avaliable, &rendering_buffer);
                assert(S_OK == hr);
                if (FAILED(hr))
                    throw std::exception("Failed to get rendering buffer.");

                std::cout << "Got buffer of " << rendering_buffer_frames_avaliable << " frames" << std::endl;

                // fill device buffer with data
                hr = FillBuffer(rendering_buffer, rendering_buffer_frames_avaliable, rendering_buffer_frames_actual, rendering_partially_processed_buffer);
                assert(SUCCEEDED(hr)); // S_OK for the fulfilled buffer and S_FALSE for partially filled buffer
                if (FAILED(hr))
                    throw std::exception("Failed to fill buffer.");

                // finally fulfilled the rendering buffer
                if (SUCCEEDED(hr))
                {
                    bool leave = (S_FALSE == hr);
                    
                    // let the device to render buffer
                    hr = m_pRenderClient->ReleaseBuffer(rendering_buffer_frames_actual, 0);
                    assert(S_OK == hr);
                    if (FAILED(hr))
                        throw std::exception("Failed to release rendering buffer.");

                    std::cout << "Released the buffer" << std::endl;

                    // bump data
                    if(out_file.is_open())
                        out_file.write((char*)rendering_buffer, rendering_buffer_frames_actual * m_format_render->bytesPerFrame);

                    // launch rendering device if not
                    if (!rendering_started)
                    {
                        // start playing.
                        hr = m_pAudioClient->Start();
                        assert(S_OK == hr);
                        if (FAILED(hr))
                            throw std::exception("Failure while calling to IAudioClient::Start.");

                        std::cout << "Started rendering" << std::endl;

                        rendering_started = true;
                    }
#ifdef _DEBUG
                    // check stream latency
                    REFERENCE_TIME rtStreamLatency = 0;
                    hr = m_pAudioClient->GetStreamLatency(&rtStreamLatency);
                    assert(S_OK == hr);
                    if (FAILED(hr))
                        throw std::exception("Failed to get stream latency.");
                    std::cout << "Stream latency " << rtStreamLatency << "." << std::endl;
#endif
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
            std::cout << "Rendering failed: " << exc.what() << std::endl;
            hr = E_FAIL;
        }
        
        if (out_file.is_open())
            out_file.close();

        return hr;
    }

    bool
    Implementation::InternalPutBuffer(std::weak_ptr<PCMDataBuffer>& buffer)
    {
        // clear free buffer
        std::shared_ptr<PCMDataBuffer> b = buffer.lock();
        b->asize = 0;
        b.reset();

        {
            // put buffer to the free buffers queue
            AutoLock l(m_cs);
            m_freeBufffersQueue.push(buffer);

            // notify waiter about new free buffer
            ReleaseSemaphore(m_hFreeDataBuffersSemaphore, 1, nullptr);
        }

        return true;
    }

    bool
    Implementation::InternalGetBuffer(std::weak_ptr<PCMDataBuffer>& buffer)
    {
        HANDLE wait_objects[]{ m_hRenderThreadExitEvent, m_hNewDataBufferSemaphore };

        // wait fot new data to arrive or until exist sinal appear
        DWORD dwResult = WaitForMultipleObjects(2, wait_objects, FALSE, INFINITE);
        if (dwResult != (WAIT_OBJECT_0 + 1))
            return false;

        {
            AutoLock l(m_cs);

            // get filled data buffer
            buffer = m_inputDataQueue.front();
            m_inputDataQueue.pop();
        }

        return true;
    }
}
