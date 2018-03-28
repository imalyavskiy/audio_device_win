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
    typedef CComPtr<IAudioRenderClient>  IAudioRenderClientPtr;     // https://msdn.microsoft.com/en-us/library/dd368242(v=vs.85).aspx
    typedef CComPtr<ISimpleAudioVolume>  ISimpleAudioVolumePtr;     // https://msdn.microsoft.com/en-us/library/dd316531(v=vs.85).aspx

    class Implementation
        : public Interface
    {
        typedef std::queue<PCMDataBuffer::wptr> BUFFER_QUEUE;
        typedef std::list<PCMDataBuffer::sptr>  BUFFER_LIST;

        inline std::streamsize frames_to_bytes(const std::streamsize& frames) const { return m_format_render->bytesPerFrame * frames; };
        inline std::streamsize bytes_to_frames(const std::streamsize& bytes) const { return bytes / m_format_render->bytesPerFrame; };

    public:
        Implementation(const std::string& dump_file);
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

        bool    InternalGetBuffer(std::weak_ptr<PCMDataBuffer>& buffer);
        bool    InternalPutBuffer(std::weak_ptr<PCMDataBuffer>& buffer);

    protected:
        ScopedCOMInitializer        m_com_guard;

        std::atomic<state>          m_state = STATE_NONE;

        UINT32                      m_rendering_buffer_frames_total = 0;
        REFERENCE_TIME              m_rendering_buffer_duration = 0;

        IMMDeviceEnumeratorPtr      m_pEnumerator;
        IMMDevicePtr                m_pDevice;
        IAudioClientPtr             m_pAudioClient;
        IAudioRenderClientPtr       m_pRenderClient;
        ISimpleAudioVolumePtr       m_pVolumeControl;

        std::unique_ptr<PCMFormat>  m_format_in;
        std::unique_ptr<size_t>     m_buffer_frames;
        std::unique_ptr<size_t>     m_buffers_total;
        std::unique_ptr<PCMFormat>  m_format_render;

        ConverterInterface::ptr     m_converter; // sample rate converter

        CriticalSection             m_cs;

        HANDLE                      m_hRenderThread = NULL;
        DWORD                       m_dwThreadId = 0;
        HANDLE                      m_hRenderThreadExitEvent = NULL;
        HANDLE                      m_hNewDataBufferSemaphore = NULL;
        HANDLE                      m_hFreeDataBuffersSemaphore = NULL;

        // queue of the buffers with rendering data
        //  populated by PutBuffer
        //  grabbed by GetBufferInternal
        BUFFER_QUEUE                m_inputDataQueue;

        // queue of the buffers that are rendered
        //  populated by PutBufferInternal
        //  grabbed by GetBuffer
        BUFFER_QUEUE                m_freeBufffersQueue;

        // overall bufer list
        //  populated by SetFormat
        BUFFER_LIST                 m_bufferStorage;

        const std::string           m_dump_file;
    };
}

#endif // __RENDER_STREAM_H__