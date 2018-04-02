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

        //
        inline std::streamsize frames_to_bytes(const std::streamsize& frames) const { return m_format_render->bytesPerFrame * frames; };
        
        //
        inline std::streamsize bytes_to_frames(const std::streamsize& bytes) const { return bytes / m_format_render->bytesPerFrame; };

    public:
        //
        Implementation(const std::string& dump_file);

        //
        ~Implementation();

        //
        bool    Init();

        //
        bool    SetFormat(const PCMFormat& format) override;

        //
        bool    GetFormat(PCMFormat& format) const override;

        //
        bool    Stop() override;

        //
        bool    WaitForCompletion() override;

        //
        state   GetState() const override;

        //
        bool    PutBuffer(PCMDataBuffer::wptr& buffer) override;

        //
        bool    GetBuffer(PCMDataBuffer::wptr& buffer) override;

    protected:
        //
        bool    Start() override;

        // S_OK for the fulfilled buffer and S_FALSE for partially filled buffer
        HRESULT FillBuffer(uint8_t * const buffer, const std::streamsize buffer_frames, std::streamsize& buffer_actual_frames, PCMDataBuffer::wptr& rendering_partially_processed_buffer);

        //
        HRESULT DoRender();

        //
        bool    InternalGetBuffer(std::weak_ptr<PCMDataBuffer>& buffer);

        //
        bool    InternalPutBuffer(std::weak_ptr<PCMDataBuffer>& buffer);

    protected:
        ScopedCOMInitializer        m_com_guard;

        std::mutex                  m_thread_running_mtx;
        std::condition_variable     m_thread_running_cv;

        std::atomic<state>          m_state = STATE_NONE;

        UINT32                      m_rendering_buffer_frames_total = 0;
        REFERENCE_TIME              m_rendering_buffer_duration = 0;

        std::shared_ptr<const PCMFormat>  m_format_render;

        IMMDeviceEnumeratorPtr      m_pEnumerator;
        IMMDevicePtr                m_pDevice;
        IAudioClientPtr             m_pAudioClient;
        IAudioRenderClientPtr       m_pRenderClient;

        SampleRateConverter::Interface::ptr m_converter; // sample rate converter

        std::thread                 m_render_thread;
        
        //
        common::DataPortInterface::wptr m_converterInputPort;

        //
        common::DataPortInterface::wptr m_converterOutputPort;

        //
        common::ThreadInterraptor   m_thread_interraption;

        //
        common::ThreadCompletor     m_thread_completor;

        // overall bufer list
        //  populated by SetFormat
        std::list<PCMDataBuffer::sptr>   m_bufferStorage;

        const std::string                m_dump_file;
    };
}

#endif // __RENDER_STREAM_H__