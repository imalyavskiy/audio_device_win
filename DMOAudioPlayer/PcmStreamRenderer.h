#ifndef __RENDER_STREAM_H__
#define __RENDER_STREAM_H__
#pragma once

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC        10000000
#define REFTIMES_PER_MILLISEC   10000

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         0x0001
#endif

// original code was takes here https://msdn.microsoft.com/en-us/library/windows/desktop/dd370802(v=vs.85).aspx

// Windows Core Audio API interfaces - https://msdn.microsoft.com/en-us/library/windows/desktop/dd370802(v=vs.85).aspx
namespace PcmSrtreamRenderer
{
    typedef CComPtr<IMMDeviceEnumerator> IMMDeviceEnumeratorPtr;    // https://msdn.microsoft.com/en-us/library/windows/desktop/dd371399(v=vs.85).aspx
    typedef CComPtr<IMMDevice>           IMMDevicePtr;              // https://msdn.microsoft.com/en-us/library/windows/desktop/dd371395(v=vs.85).aspx
    typedef CComPtr<IAudioClient>        IAudioClientPtr;           // https://msdn.microsoft.com/en-us/library/windows/desktop/dd370865(v=vs.85).aspx
    typedef CComPtr<IAudioRenderClient>  IAudioRenderClientPtr;

    class Implementation
        : public Interface
    {
    public:
        Implementation();
        ~Implementation();

        bool    Init();

        bool    SetFormat(const PCMFormat& format, const size_t buffer_frames, const size_t buffers_total) override;
        bool    GetFormat(PCMFormat& format, size_t& buffer_frames, size_t& buffers_total) const override;

        bool    Start() override;
        bool    Stop() override;

        state   GetState() const override;

        bool    PutBuffer(std::weak_ptr<PCMDataBuffer>& buffer) override;
        bool    GetBuffer(std::weak_ptr<PCMDataBuffer>& buffer) override;

    protected:
        static DWORD WINAPI DoRenderThread(LPVOID param);

        HRESULT DoRender();
        HRESULT ProcessBuffer(std::weak_ptr<PCMDataBuffer> buffer);

        bool    InternalGetBuffer(std::weak_ptr<PCMDataBuffer>& buffer);
        bool    InternalPutBuffer(std::weak_ptr<PCMDataBuffer>& buffer);

    protected:
        ScopedCOMInitializer        m_com_guard;

        state                       m_state = STATE_NONE;

        IMMDeviceEnumeratorPtr      m_pEnumerator;
        IMMDevicePtr                m_pDevice;
        IAudioClientPtr             m_pAudioClient;
        IAudioRenderClientPtr       m_pRenderClient;

        std::unique_ptr<PCMFormat>  m_format_in;
        std::unique_ptr<size_t>     m_buffer_frames;
        std::unique_ptr<size_t>     m_buffers_total;
        std::unique_ptr<PCMFormat>  m_format_render;

        ConverterInterface::ptr     m_converter; // sample rate converter

        CriticalSection             m_cs;

        HANDLE                      m_hRenderThread;
        DWORD                       m_dwThreadId;
        HANDLE                      m_hRenderThreadExitEvent;
        HANDLE                      m_hNewDataBufferSemaphore;
        HANDLE                      m_hFreeDataBuffersSemaphore;

        typedef std::queue<PCMDataBuffer::wptr> BUFFER_QUEUE;
        typedef std::list<PCMDataBuffer::sptr>  BUFFER_LIST;

        // queue of the buffers with rendering data
        //  populated by PutBuffer
        //  grabbed by GetBufferInternal
        BUFFER_QUEUE                m_inputDataQueue;

        // queue of the buffers that are rendered
        //  populated by PutBufferInternal
        //  grabbed by GetBuffer
        BUFFER_QUEUE                m_freeBufffers;

        // overall bufer list
        //  populated by SetFormat
        BUFFER_LIST   m_bufferStorage;

        // audio rendering endpoint buffer total frame count
        uint32_t                    m_bufferFrameCount;
    };
}

#endif // __RENDER_STREAM_H__