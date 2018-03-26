
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

        //
        if (FAILED(hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, rtRequestedDuration, 0, reinterpret_cast<WAVEFORMATEX*>(p_mix_format.get()), NULL)))
            return SUCCEEDED(hr);

        //
        m_format_render.reset(new PCMFormat{ p_mix_format->nSamplesPerSec, p_mix_format->nChannels, p_mix_format->wBitsPerSample, p_mix_format->nBlockAlign });

        // Get the actual size of the allocated buffer.
        if (FAILED(hr = m_pAudioClient->GetBufferSize(&m_bufferFrameCount)))
            return hr;

        // update state - we are ready to consume data
        m_state = STATE_INITIAL;

        return true;
    }

    bool
    Implementation::SetFormat(const PCMFormat& format, const size_t buffer_frames, const size_t buffers_total)
    {
        assert(m_state == STATE_INITIAL);
        if (m_state != STATE_INITIAL)
            return false;

        m_format_in.reset(new PCMFormat(format));
        m_buffer_frames.reset(new size_t(buffer_frames));
        m_buffers_total.reset(new size_t(buffers_total));

        m_state = STATE_STOPPED;

        const size_t buffer_size = m_format_in->bytesPerFrame * buffer_frames;

        for(size_t cBuffer = 0; cBuffer < buffers_total; ++cBuffer)
        {
            PCMDataBuffer::sptr pB;
            pB.reset(new PCMDataBuffer{ new uint8_t[buffer_size], 0, (const uint32_t)buffer_size });
            m_bufferStorage.push_back(pB);
            m_freeBufffersQueue.push(pB);
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

        assert(m_state == STATE_STOPPED);
        if (m_state != STATE_STOPPED)
            return false;

        m_hRenderThread = CreateThread(NULL, 0, &Implementation::DoRenderThread, LPVOID(this), 0, &m_dwThreadId);

        m_state = STATE_STARTED;

        return true;
    }

    bool
    Implementation::Stop()
    {
        HRESULT hr = S_OK;
        assert(m_state == STATE_STARTED);
        if (m_state != STATE_STARTED)
            return false;

        if (m_hRenderThread)
        {
            BOOL bResult = FALSE;
            bResult = SetEvent(m_hRenderThreadExitEvent);
            assert(TRUE == bResult);

            const DWORD dwResult = WaitForSingleObject(m_hRenderThread, 10000);
            if (dwResult != WAIT_OBJECT_0)
                throw std::logic_error("rendere thread stucked.");

            CloseHandle(m_hRenderThread);
            m_hRenderThread = NULL;
        }

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
        AutoLock l(m_cs);

        {
            // but filled buffer to the data queue
            m_inputDataQueue.push(std::move(buffer));
            buffer.reset();

            // notify render thread about new data arrived
            BOOL result = ReleaseSemaphore(m_hNewDataBufferSemaphore, 1, nullptr);
            assert(result == FALSE);
        }

        return false;
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
        if (param != NULL)
            return ((Implementation*)param)->DoRender();

        return E_FAIL;
    }

    HRESULT
    Implementation::DoRender()
    {
        HRESULT hr = S_OK;
        BOOL bStarted = FALSE;

        HANDLE wait_objects[]{ m_hRenderThreadExitEvent, m_hNewDataBufferSemaphore };
        DWORD dwWaitResult = 0;

        // render loop
        while (true)
        {
            dwWaitResult = WaitForMultipleObjects(sizeof wait_objects / sizeof wait_objects[0], wait_objects, FALSE, INFINITE);
            if (dwWaitResult == WAIT_OBJECT_0)
                break;
            else if (dwWaitResult == WAIT_OBJECT_0 + 1)
            {
                std::weak_ptr<PCMDataBuffer> buffer;

                {
                    AutoLock l(m_cs);
                    buffer = m_inputDataQueue.front();
                    m_inputDataQueue.pop();
                }

                hr = ProcessBuffer(buffer);
                assert(SUCCEEDED(hr));

                {
                    AutoLock l(m_cs);
                    m_freeBufffersQueue.push(buffer);
                    ReleaseSemaphore(m_hFreeDataBuffersSemaphore, 1, nullptr);
                }

                if (FALSE == bStarted)
                {
                    if (FAILED(hr = m_pAudioClient->Start()))  // Start playing.
                    {
                        assert(0);
                        return SUCCEEDED(hr);
                    }

                    bStarted = TRUE;
                }
            }
        }

        if (TRUE == bStarted)
        {
            if (FAILED(hr = m_pAudioClient->Stop()))  // Stop playing.
            {
                assert(0);
                return SUCCEEDED(hr);
            }
        }

        return S_OK;
    }

    HRESULT
    Implementation::ProcessBuffer(std::weak_ptr<PCMDataBuffer> buffer)
    {
        throw std::logic_error("not implemented");
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
#include "AudioSourceInterface.h"

HRESULT PlayAudioStream(WavAudioSource::Interface *pMySource)
{
    HRESULT                 hr = S_OK;

    PcmSrtreamRenderer::IMMDeviceEnumeratorPtr  pEnumerator;
    PcmSrtreamRenderer::IMMDevicePtr            pDevice;
    PcmSrtreamRenderer::IAudioClientPtr         pAudioClient;
    PcmSrtreamRenderer::IAudioRenderClientPtr   pRenderClient;

    ComUniquePtr<WAVEFORMATEX>          p_mix_format(nullptr, nullptr);
    ComUniquePtr<WAVEFORMATEXTENSIBLE>  p_desired_format(nullptr, nullptr);
    ComUniquePtr<WAVEFORMATEX>          p_closest_format(nullptr, nullptr);

    REFERENCE_TIME          rtRequestedDuration = REFTIMES_PER_SEC;
    REFERENCE_TIME          rtActualDuration = 0;

    UINT32                  bufferFrameCount = 0;
    UINT32                  numFramesAvailable = 0;
    UINT32                  numFramesPadding = 0;

    BYTE*                   pData = NULL;
    DWORD                   flags = 0;

    ScopedCOMInitializer com_guard;

    // create multimedia device enumerator
    if (FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator)))
        return hr;

    // retrieve the default audio endpoint for the specified data-flow direction and role.
    if (FAILED(hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) // see https://msdn.microsoft.com/en-us/library/windows/desktop/dd370813(v=vs.85).aspx for eConsole
        return hr;

    // create AudioClient
    if (FAILED(hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient)))
        return hr;

    // retrieve the stream format that the audio engine uses for its internal processing of shared-mode streams
    {
        WAVEFORMATEX* tmp;
        if (FAILED(hr = pAudioClient->GetMixFormat(&tmp)))
            return hr;
        p_mix_format.reset(tmp);
    }

    //
    PCMFormat format{0,0,0,0};

    if (FAILED(hr = pMySource->GetFormat(format)))
        return hr;

    {   // check if the proposed format can be consumed
        WAVEFORMATEXTENSIBLE wfex;
        wfex.Format.wFormatTag = 0xfffe;
        wfex.Format.nChannels = format.channels;
        wfex.Format.nSamplesPerSec = format.samplesPerSecond;
        wfex.Format.nAvgBytesPerSec = format.samplesPerSecond * format.bytesPerFrame;
        wfex.Format.nBlockAlign = format.bytesPerFrame;
        wfex.Format.wBitsPerSample = format.bitsPerSample;
        wfex.Format.cbSize = 22;
        wfex.Samples.wValidBitsPerSample = format.bitsPerSample;
        wfex.dwChannelMask = 0x3;
        wfex.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

        WAVEFORMATEX* tmp;
        hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, reinterpret_cast<WAVEFORMATEX*>(p_desired_format.get()), &tmp);

        p_closest_format.reset(tmp);

        if (S_FALSE == hr)
            return E_FAIL;
    }

    //
    if (FAILED(hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, rtRequestedDuration, 0, reinterpret_cast<WAVEFORMATEX*>(p_desired_format.get()), NULL)))
        return hr;

    // Get the actual size of the allocated buffer.
    if (FAILED(hr = pAudioClient->GetBufferSize(&bufferFrameCount)))
        return hr;

    //
    if (FAILED(hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient)))
        return hr;

    // Grab the entire buffer for the initial fill operation.
    if (FAILED(hr = pRenderClient->GetBuffer(bufferFrameCount, &pData)))
        return hr;

    // Load the initial data into the shared buffer.
    if (FAILED(hr = pMySource->ReadData(bufferFrameCount, pData, &flags)))
        return hr;

    if (FAILED(hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags)))
        return hr;

    // Calculate the actual duration of the allocated buffer.
    rtActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / p_desired_format->Format.nSamplesPerSec;

    if (FAILED(hr = pAudioClient->Start()))  // Start playing.
        return hr;

    // Each loop fills about half of the shared buffer.
    while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
    {
        // Sleep for half the buffer duration.
        Sleep((DWORD)(rtActualDuration / REFTIMES_PER_MILLISEC / 2));

        // See how much buffer space is available.
        if (FAILED(hr = pAudioClient->GetCurrentPadding(&numFramesPadding)))
            return hr;

        numFramesAvailable = bufferFrameCount - numFramesPadding;

        // Grab all the available space in the shared buffer.
        if (FAILED(hr = pRenderClient->GetBuffer(numFramesAvailable, &pData)))
            return hr;

        // Get next 1/2-second of data from the audio source.
        if (FAILED(hr = pMySource->ReadData(numFramesAvailable, pData, &flags)))
            return hr;


        if (FAILED(hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags)))
            return hr;
    }

    // Wait for last data in buffer to play before stopping.
    Sleep((DWORD)(rtActualDuration / REFTIMES_PER_MILLISEC / 2));

    if (FAILED(hr = pAudioClient->Stop()))  // Stop playing.
        return hr;

    return hr;
}
