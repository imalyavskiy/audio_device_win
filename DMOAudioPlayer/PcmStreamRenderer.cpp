
#include "stdafx.h"
#include "common.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "PcmStreamRenderer.h"

const size_t DATA_BUFFERS_MAX = 100;
namespace PcmSrtreamRenderer
{
    Implementation::Implementation()
        : m_hRenderThread(NULL)
        , m_dwThreadId(0)
        , m_hRenderThreadExitEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
        , m_hNewDataBufferSemaphore(CreateSemaphore(NULL, 0, DATA_BUFFERS_MAX, NULL))
        , m_hFreeDataBuffersSemaphore(CreateSemaphore(NULL, 0, DATA_BUFFERS_MAX, NULL))
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
        REFERENCE_TIME              rtRequestedDuration = REFTIMES_PER_SEC;

        // check state
        assert(m_state == STATE_NONE);
        if (m_state != STATE_NONE)
            return false;

        // create multimedia device enumerator
        if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator)))
            return SUCCEEDED(hr);

        // retrieve the default audio endpoint for the specified data-flow direction and role.
        if (FAILED(hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice))) // see https://msdn.microsoft.com/en-us/library/windows/desktop/dd370813(v=vs.85).aspx for eConsole
            return SUCCEEDED(hr);

        // create AudioClient
        if (FAILED(hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient)))
            return SUCCEEDED(hr);

        // retrieve the stream format that the audio engine uses for its internal processing of shared-mode streams
        {
            WAVEFORMATEX* tmp;
            if (FAILED(hr = m_pAudioClient->GetMixFormat(&tmp)))
                return SUCCEEDED(hr);
            p_mix_format = ComUniquePtr<WAVEFORMATEX>{ tmp, &CoTaskMemFree};
        }

        // apply the rendering format
        if (FAILED(hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, rtRequestedDuration, 0, reinterpret_cast<WAVEFORMATEX*>(p_mix_format.get()), NULL)))
            return SUCCEEDED(hr);

        // keep the rendering format
        m_format_render.reset(new PCMFormat{ p_mix_format->nSamplesPerSec, p_mix_format->nChannels, p_mix_format->wBitsPerSample, p_mix_format->nBlockAlign });

        // Get the actual size of the allocated buffer.
        if (FAILED(hr = m_pAudioClient->GetBufferSize(&m_rendering_buffer_frames_total)))
            return hr;

        // retireve the renderer client pointer
        if (FAILED(hr = m_pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_pRenderClient)))
            return hr;

        // calculate rendering buffer total duration
        m_rendering_buffer_duration = ((double)REFTIMES_PER_SEC * m_rendering_buffer_frames_total) / m_format_render->samplesPerSecond;

        REFERENCE_TIME rtStreamLatency = 0;
        if (FAILED(hr = m_pAudioClient->GetStreamLatency(&rtStreamLatency)))
            return hr;

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
            if (WAIT_TIMEOUT == WaitForSingleObject(m_hRenderThread, 10000))
            {
                // something went wrong
                throw std::logic_error("rendere thread stucked.");
            }

            // finally close it
            CloseHandle(m_hRenderThread);
            m_hRenderThread = NULL;
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
    Implementation::DoRender()
    {
        HRESULT hr = S_OK;

        // render loop
        while (true)
        {
            std::shared_ptr<PCMDataBuffer> sbuffer;

            // shose buffer
            if (!m_rendering_partially_processed_buffer.expired())
            {// take the partially processed one
                sbuffer = m_rendering_partially_processed_buffer.lock();
                m_rendering_partially_processed_buffer.reset();
            }
            else
            {// or brand new
                std::weak_ptr<PCMDataBuffer> wbuffer;

                if (!InternalGetBuffer(wbuffer))
                    return E_FAIL;

                if (wbuffer.expired())
                    return E_FAIL;

                sbuffer = wbuffer.lock();
            }

            // take the rendering buffer
            if (!m_rendering_buffer)
            {
                UINT32 rendering_buffer_frames_padding = 0;
                // see how much buffer space is available.
                if (FAILED(hr = m_pAudioClient->GetCurrentPadding(&rendering_buffer_frames_padding)))
                    return hr;

                // calc avaliable buffer frames
                m_rendering_buffer_frames_avaliable = m_rendering_buffer_frames_total - rendering_buffer_frames_padding;

                // grab the avaliable buffer
                if (FAILED(hr = m_pRenderClient->GetBuffer(m_rendering_buffer_frames_avaliable, &m_rendering_buffer)))
                    return hr;

                std::cout << "Got buffer of " << m_rendering_buffer_frames_avaliable << " frames" << std::endl;

                // at this point rest frames are of the same value as avaliable
                m_rendering_buffer_frames_rest = m_rendering_buffer_frames_avaliable;
            }

            // offset in the rendering buffer
            const uint32_t offset_frames = m_rendering_buffer_frames_avaliable - m_rendering_buffer_frames_rest;

            // bytes to copy from the source buffer
            const uint32_t frames_to_render = m_rendering_buffer_frames_rest < bytes_to_frames(sbuffer->asize) ? m_rendering_buffer_frames_rest : bytes_to_frames(sbuffer->asize);

            // copy bytes_to_render bytes from source buffer to rendering buffer
            memcpy(m_rendering_buffer + frames_to_bytes(offset_frames), sbuffer->p, frames_to_bytes(frames_to_render));

            // reduce the rest of rendering buffer with copied data 
            m_rendering_buffer_frames_rest -= frames_to_render;

            // once hte buffer had processed partially - keep it for the next session
            if (frames_to_render < bytes_to_frames(sbuffer->asize))
            {
                // how many bytes to keed in the source buffer
                sbuffer->asize -= frames_to_bytes(frames_to_render);

                // move kept bytes to the source buffer beginning
                memmove(sbuffer->p, ((char*)sbuffer->p + frames_to_bytes(frames_to_render)), sbuffer->asize);

                // keep the buffer separately
                m_rendering_partially_processed_buffer = sbuffer;

                // prevent partially processed buffer from reaching free buffers queue
                sbuffer.reset();
            }

            // finally fulfilled the rendering buffer
            if (0 == m_rendering_buffer_frames_rest)
            {
                // TODO: the last parameter is here https://msdn.microsoft.com/en-us/library/windows/desktop/dd371458(v=vs.85).aspx
                hr = m_pRenderClient->ReleaseBuffer(m_rendering_buffer_frames_avaliable, 0);

                std::cout << "Released the buffer" << std::endl;

                m_rendering_buffer = NULL;

                if (!m_rendering_started)
                {
                    if (FAILED(hr = m_pAudioClient->Start()))  // start playing.
                        return hr;

                    std::cout << "Started rendering" << std::endl;

                    m_rendering_started = true;
                }

                REFERENCE_TIME rtStreamLatency = 0;
                if (FAILED(hr = m_pAudioClient->GetStreamLatency(&rtStreamLatency)))
                    return hr;

                std::cout << "Stream latency " << rtStreamLatency << "." << std::endl;

                // sleep for half the buffer duration.
                Sleep((DWORD)(m_rendering_buffer_duration / REFTIMES_PER_MILLISEC / 2));
            }

            if (sbuffer)
            {
                std::weak_ptr<PCMDataBuffer> wbuffer(sbuffer);
                if (!InternalPutBuffer(wbuffer))
                    return E_FAIL;
            }
        }

        return S_OK;
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
