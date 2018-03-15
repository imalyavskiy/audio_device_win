#ifndef __DEVICE_H__
#define __DEVICE_H__
#pragma once

#if (_MSC_VER >= 1400)  // only include for VS 2005 and higher

//#include "modules/audio_device/audio_device_generic.h"

#include <wmcodecdsp.h>      // CLSID_CWMAudioAEC
// (must be before audioclient.h)
#include <Audioclient.h>     // WASAPI
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>     // MMDevice
#include <avrt.h>            // Avrt
#include <endpointvolume.h>
#include <mediaobj.h>        // IMediaObject

//#include "rtc_base/criticalsection.h"
//#include "rtc_base/scoped_ref_ptr.h"

// Use Multimedia Class Scheduler Service (MMCSS) to boost the thread priority
#pragma comment( lib, "avrt.lib" )

// AVRT function pointers
typedef BOOL(WINAPI *PAvRevertMmThreadCharacteristics)(HANDLE);
typedef HANDLE(WINAPI *PAvSetMmThreadCharacteristicsA)(LPCSTR, LPDWORD);
typedef BOOL(WINAPI *PAvSetMmThreadPriority)(HANDLE, AVRT_PRIORITY);

namespace rtc
{
    void SetCurrentThreadName(const char* name);
}

namespace webrtc 
{
    const float     MAX_CORE_SPEAKER_VOLUME             = 255.0f;
    const float     MIN_CORE_SPEAKER_VOLUME             = 0.0f;
    const float     MAX_CORE_MICROPHONE_VOLUME          = 255.0f;
    const float     MIN_CORE_MICROPHONE_VOLUME          = 0.0f;
    const uint16_t  CORE_SPEAKER_VOLUME_STEP_SIZE       = 1;
    const uint16_t  CORE_MICROPHONE_VOLUME_STEP_SIZE    = 1;

    class AudioDeviceWindowsCore : public AudioDeviceGeneric
    {
    public:
        AudioDeviceWindowsCore();
        ~AudioDeviceWindowsCore();

        static bool CoreAudioIsSupported();

        // Retrieve the currently utilized audio layer
        virtual int32_t ActiveAudioLayer(AudioDeviceModule::AudioLayer& audioLayer) const;

        // Main initializaton and termination
        virtual InitStatus Init();
        virtual int32_t Terminate();
        virtual bool    Initialized() const;

        // Device enumeration
        virtual int16_t PlayoutDevices();
        virtual int16_t RecordingDevices();
        virtual int32_t PlayoutDeviceName(const uint16_t index, std::wstring& name, std::wstring& guid) override;
        virtual int32_t RecordingDeviceName(const uint16_t index, std::wstring& name, std::wstring& guid) override;

        // Device selection
        virtual int32_t SetPlayoutDevice(uint16_t index);
        virtual int32_t SetPlayoutDevice(AudioDeviceModule::WindowsDeviceType device);
        virtual int32_t SetRecordingDevice(uint16_t index);
        virtual int32_t SetRecordingDevice(AudioDeviceModule::WindowsDeviceType device);

        // Audio transport initialization
        virtual int32_t PlayoutIsAvailable(bool& available);
        virtual int32_t InitPlayout();
        virtual bool    PlayoutIsInitialized() const;
        virtual int32_t RecordingIsAvailable(bool& available);
        virtual int32_t InitRecording();
        virtual bool    RecordingIsInitialized() const;

        // Audio transport control
        virtual int32_t StartPlayout();
        virtual int32_t StopPlayout();
        virtual bool    Playing() const;
        virtual int32_t StartRecording();
        virtual int32_t StopRecording();
        virtual bool    Recording() const;

        // Audio mixer initialization
        virtual int32_t InitSpeaker();
        virtual bool    SpeakerIsInitialized() const;
        virtual int32_t InitMicrophone();
        virtual bool    MicrophoneIsInitialized() const;

        // Speaker volume controls
        virtual int32_t SpeakerVolumeIsAvailable(bool& available);
        virtual int32_t SetSpeakerVolume(uint32_t volume);
        virtual int32_t SpeakerVolume(uint32_t& volume) const;
        virtual int32_t MaxSpeakerVolume(uint32_t& maxVolume) const;
        virtual int32_t MinSpeakerVolume(uint32_t& minVolume) const;

        // Microphone volume controls
        virtual int32_t MicrophoneVolumeIsAvailable(bool& available);
        virtual int32_t SetMicrophoneVolume(uint32_t volume);
        virtual int32_t MicrophoneVolume(uint32_t& volume) const;
        virtual int32_t MaxMicrophoneVolume(uint32_t& maxVolume) const;
        virtual int32_t MinMicrophoneVolume(uint32_t& minVolume) const;

        // Speaker mute control
        virtual int32_t SpeakerMuteIsAvailable(bool& available);
        virtual int32_t SetSpeakerMute(bool enable);
        virtual int32_t SpeakerMute(bool& enabled) const;

        // Microphone mute control
        virtual int32_t MicrophoneMuteIsAvailable(bool& available);
        virtual int32_t SetMicrophoneMute(bool enable);
        virtual int32_t MicrophoneMute(bool& enabled) const;

        // Stereo support
        virtual int32_t StereoPlayoutIsAvailable(bool& available);
        virtual int32_t SetStereoPlayout(bool enable);
        virtual int32_t StereoPlayout(bool& enabled) const;
        virtual int32_t StereoRecordingIsAvailable(bool& available);
        virtual int32_t SetStereoRecording(bool enable);
        virtual int32_t StereoRecording(bool& enabled) const;

        // Delay information and control
        virtual int32_t PlayoutDelay(uint16_t& delayMS) const;

        virtual int32_t EnableBuiltInAEC(bool enable);

    public:
        virtual void AttachAudioBuffer(AudioDeviceBuffer* audioBuffer);

    private:
        bool KeyPressed() const;

    private:    // avrt function pointers
        PAvRevertMmThreadCharacteristics    _PAvRevertMmThreadCharacteristics;
        PAvSetMmThreadCharacteristicsA      _PAvSetMmThreadCharacteristicsA;
        PAvSetMmThreadPriority              _PAvSetMmThreadPriority;
        HMODULE                             _avrtLibrary;
        bool                                _winSupportAvrt;

    private:    // thread functions
        DWORD InitCaptureThreadPriority();
        void RevertCaptureThreadPriority();
        
        static DWORD WINAPI WSAPICaptureThread(LPVOID context);
        DWORD DoCaptureThread();

        static DWORD WINAPI WSAPICaptureThreadPollDMO(LPVOID context);
        DWORD DoCaptureThreadPollDMO();

        static DWORD WINAPI WSAPIRenderThread(LPVOID context);
        
        DWORD   DoRenderThread();

        void    _Lock() { m_critSect.Enter(); };
        void    _UnLock() { m_critSect.Leave(); };

        int     SetDMOProperties();

        int     SetBoolProperty(IPropertyStore* ptrPS, REFPROPERTYKEY key, VARIANT_BOOL value);

        int     SetVtI4Property(IPropertyStore* ptrPS, REFPROPERTYKEY key, LONG value);

        int32_t _EnumerateEndpointDevicesAll(EDataFlow dataFlow) const;
        void    _TraceCOMError(HRESULT hr) const;

        int32_t _RefreshDeviceList(EDataFlow dir);
        int16_t _DeviceListCount(EDataFlow dir);
        int32_t _GetDefaultDeviceName(EDataFlow dir, ERole role, LPWSTR szBuffer, int bufferLen);
        int32_t _GetListDeviceName(EDataFlow dir, int index, LPWSTR szBuffer, int bufferLen);
        int32_t _GetDeviceName(IMMDevice* pDevice, LPWSTR pszBuffer, int bufferLen);
        int32_t _GetListDeviceID(EDataFlow dir, int index, LPWSTR szBuffer, int bufferLen);
        int32_t _GetDefaultDeviceID(EDataFlow dir, ERole role, LPWSTR szBuffer, int bufferLen);
        int32_t _GetDefaultDeviceIndex(EDataFlow dir, ERole role, int* index);
        int32_t _GetDeviceID(IMMDevice* pDevice, LPWSTR pszBuffer, int bufferLen);
        int32_t _GetDefaultDevice(EDataFlow dir, ERole role, IMMDevice** ppDevice);
        int32_t _GetListDevice(EDataFlow dir, int index, IMMDevice** ppDevice);

        // Converts from wide-char to UTF-8 if UNICODE is defined.
        // Does nothing if UNICODE is undefined.
        char* WideToUTF8(const TCHAR* src) const;

        int32_t InitRecordingDMO();

        ScopedCOMInitializer                    m_comInit;
        AudioDeviceBuffer*                      m_ptrAudioBuffer;

        mutable CriticalSection                 m_critSect;
        mutable CriticalSection                 m_volumeMutex;

        CComPtr<IMMDeviceEnumerator>            m_ptrEnumerator;
        CComPtr<IMMDeviceCollection>            m_ptrRenderCollection;
        CComPtr<IMMDeviceCollection>            m_ptrCaptureCollection;
        CComPtr<IMMDevice>                      m_ptrDeviceOut;
        CComPtr<IMMDevice>                      m_ptrDeviceIn;

        CComPtr<IAudioClient>                   m_ptrClientOut;
        CComPtr<IAudioClient>                   m_ptrClientIn;
        CComPtr<IAudioRenderClient>             m_ptrRenderClient;
        CComPtr<IAudioCaptureClient>            m_ptrCaptureClient;
        CComPtr<IAudioEndpointVolume>           m_ptrCaptureVolume;
        CComPtr<ISimpleAudioVolume>             m_ptrRenderSimpleVolume;

        // DirectX Media Object (DMO) for the built-in AEC.
        CComPtr<IMediaObject>                   m_dmo;
        CComPtr<IMediaBuffer>                   m_mediaBuffer;
        bool                                    m_builtInAecEnabled;

        HANDLE                                  m_hRenderSamplesReadyEvent;
        HANDLE                                  m_hPlayThread;
        HANDLE                                  m_hRenderStartedEvent;
        HANDLE                                  m_hShutdownRenderEvent;

        HANDLE                                  m_hCaptureSamplesReadyEvent;
        HANDLE                                  m_hRecThread;
        HANDLE                                  m_hCaptureStartedEvent;
        HANDLE                                  m_hShutdownCaptureEvent;

        HANDLE                                  m_hMmTask;

        UINT                                    m_playAudioFrameSize;
        uint32_t                                m_playSampleRate;
        uint32_t                                m_devicePlaySampleRate;
        uint32_t                                m_playBlockSize;
        uint32_t                                m_devicePlayBlockSize;
        uint32_t                                m_playChannels;
        uint32_t                                m_sndCardPlayDelay;
        UINT64                                  m_writtenSamples;

        UINT                                    m_recAudioFrameSize;
        uint32_t                                m_recSampleRate;
        uint32_t                                m_recBlockSize;
        uint32_t                                m_recChannels;
        UINT64                                  m_readSamples;
        uint32_t                                m_sndCardRecDelay;

        uint16_t                                m_recChannelsPrioList[3];
        uint16_t                                m_playChannelsPrioList[2];

        LARGE_INTEGER                           m_perfCounterFreq;
        double                                  m_perfCounterFactor;

    private:
        bool                                    m_initialized;
        bool                                    m_recording;
        bool                                    m_playing;
        bool                                    m_recIsInitialized;
        bool                                    m_playIsInitialized;
        bool                                    m_speakerIsInitialized;
        bool                                    m_microphoneIsInitialized;

        bool                                    m_usingInputDeviceIndex;
        bool                                    m_usingOutputDeviceIndex;
        AudioDeviceModule::WindowsDeviceType    m_inputDevice;
        AudioDeviceModule::WindowsDeviceType    m_outputDevice;
        uint16_t                                m_inputDeviceIndex;
        uint16_t                                m_outputDeviceIndex;

        uint16_t                                m_playBufDelay;

        mutable char                            m_str[512];
    };

#endif    // #if (_MSC_VER >= 1400)

}  // namespace webrtc


#endif // __DEVICE_H__
