#include "stdafx.h"
#include "AudioDeviceBuffer.h"
#include "AudioDeviceGeneric.h"
#include "AudioDeviceWindowsCore.h"

/*
*  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
*
*  Use of this source code is governed by a BSD-style license
*  that can be found in the LICENSE file in the root of the source
*  tree. An additional intellectual property rights grant can be found
*  in the file PATENTS.  All contributing project authors may
*  be found in the AUTHORS file in the root of the source tree.
*/

#pragma warning(disable : 4995)  // name was marked as #pragma deprecated

#if (_MSC_VER >= 1310) && (_MSC_VER < 1400)
// Reports the major and minor versions of the compiler.
// For example, 1310 for Microsoft Visual C++ .NET 2003. 1310 represents version
// 13 and a 1.0 point release. The Visual C++ 2005 compiler version is 1400.
// Type cl /? at the command line to see the major and minor versions of your
// compiler along with the build number.
#pragma message(">> INFO: Windows Core Audio is not supported in VS 2003")
#endif

//#include "modules/audio_device/audio_device_config.h"

//#ifdef WEBRTC_WINDOWS_CORE_AUDIO_BUILD

//#include "modules/audio_device/win/audio_device_core_win.h"

#include <assert.h>
#include <string.h>

#include <Functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <dmo.h>
#include <mmsystem.h>
#include <strsafe.h>
#include <uuids.h>
#include <windows.h>

#include <iomanip>

//#include "rtc_base/logging.h"
//#include "rtc_base/platform_thread.h"
//#include "system_wrappers/include/sleep.h"

// Macro that calls a COM method returning HRESULT value.
#define EXIT_ON_ERROR(hres) do { if (FAILED(hres)) goto Exit; } while (0)

// Macro that continues to a COM error.
#define CONTINUE_ON_ERROR(hres) do { if (FAILED(hres)) goto Next; } while (0)

// Macro that releases a COM object if not NULL.
#define SAFE_RELEASE(p) do { if ((p)) { (p)->Release(); (p) = NULL; } } while (0)

#define ROUND(x) ((x) >= 0 ? (int)((x) + 0.5) : (int)((x)-0.5))

// REFERENCE_TIME time units per millisecond
#define REFTIMES_PER_MILLISEC 10000

typedef struct tagTHREADNAME_INFO {
    DWORD dwType;      // must be 0x1000
    LPCSTR szName;     // pointer to name (in user addr space)
    DWORD dwThreadID;  // thread ID (-1=caller thread)
    DWORD dwFlags;     // reserved for future use, must be zero
} THREADNAME_INFO;

namespace rtc
{
    void SetCurrentThreadName(const char* name)
    {
        THREADNAME_INFO threadname_info{ 0x1000, name, static_cast<DWORD>(-1), 0 };

        __try
        {
            ::RaiseException(0x406D1388, 0, sizeof(threadname_info) / sizeof(DWORD), reinterpret_cast<ULONG_PTR*>(&threadname_info));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ;
        }
    }
}

namespace webrtc 
{
    namespace 
    {

        enum { COM_THREADING_MODEL = COINIT_MULTITHREADED };

        enum { kAecCaptureStreamIndex = 0, kAecRenderStreamIndex = 1 };

        // An implementation of IMediaBuffer, as required for
        // IMediaObject::ProcessOutput(). After consuming data provided by
        // ProcessOutput(), call SetLength() to update the buffer availability.
        //
        // Example implementation:
        // http://msdn.microsoft.com/en-us/library/dd376684(v=vs.85).aspx
        class MediaBufferImpl 
            : public IMediaBuffer 
        {
        public:
            explicit MediaBufferImpl(DWORD maxLength)
                : _data(new BYTE[maxLength])
                , _length(0)
                , _maxLength(maxLength)
                , _refCount(0) 
            {}

            // IMediaBuffer methods.
            STDMETHOD(GetBufferAndLength(BYTE** ppBuffer, DWORD* pcbLength)) 
            {
                if (!ppBuffer || !pcbLength) {
                    return E_POINTER;
                }

                *ppBuffer = _data;
                *pcbLength = _length;

                return S_OK;
            }

            STDMETHOD(GetMaxLength(DWORD* pcbMaxLength)) 
            {
                if (!pcbMaxLength) {
                    return E_POINTER;
                }

                *pcbMaxLength = _maxLength;
                return S_OK;
            }

            STDMETHOD(SetLength(DWORD cbLength)) 
            {
                if (cbLength > _maxLength) {
                    return E_INVALIDARG;
                }

                _length = cbLength;
                return S_OK;
            }

            // IUnknown methods.
            STDMETHOD_(ULONG, AddRef()) 
            { 
                return InterlockedIncrement(&_refCount); 
            }

            STDMETHOD(QueryInterface(REFIID riid, void** ppv)) 
            {
                if (!ppv) {
                    return E_POINTER;
                }
                else if (riid != IID_IMediaBuffer && riid != IID_IUnknown) {
                    return E_NOINTERFACE;
                }

                *ppv = static_cast<IMediaBuffer*>(this);
                AddRef();
                return S_OK;
            }

            STDMETHOD_(ULONG, Release()) 
            {
                LONG refCount = InterlockedDecrement(&_refCount);
                if (refCount == 0) 
                    delete this;

                return refCount;
            }

        private:
            ~MediaBufferImpl() 
            { 
                delete[] _data; 
            }

            BYTE* _data;
            DWORD _length;
            const DWORD _maxLength;
            LONG _refCount;
        };
    }  // namespace

    // ============================================================================
    //                              Static Methods
    // ============================================================================

    // ----------------------------------------------------------------------------
    //  CoreAudioIsSupported
    // ----------------------------------------------------------------------------

    bool 
    AudioDeviceWindowsCore::CoreAudioIsSupported() 
    {
        RTC_LOG(LS_VERBOSE << __FUNCTION__ );

        bool MMDeviceIsAvailable(false);
        bool coreAudioIsSupported(false);

        HRESULT hr(S_OK);
        TCHAR buf[MAXERRORLENGTH];
        TCHAR errorText[MAXERRORLENGTH];

        // 1) Check if Windows version is Vista SP1 or later.
        //
        // CoreAudio is only available on Vista SP1 and later.
        //
        OSVERSIONINFOEX osvi;
        DWORDLONG dwlConditionMask = 0;
        int op = VER_LESS_EQUAL;

        // Initialize the OSVERSIONINFOEX structure.
        ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
        osvi.dwMajorVersion = 6;
        osvi.dwMinorVersion = 0;
        osvi.wServicePackMajor = 0;
        osvi.wServicePackMinor = 0;
        osvi.wProductType = VER_NT_WORKSTATION;

        // Initialize the condition mask.
        VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, op);
        VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, op);
        VER_SET_CONDITION(dwlConditionMask, VER_SERVICEPACKMAJOR, op);
        VER_SET_CONDITION(dwlConditionMask, VER_SERVICEPACKMINOR, op);
        VER_SET_CONDITION(dwlConditionMask, VER_PRODUCT_TYPE, VER_EQUAL);

        DWORD dwTypeMask = VER_MAJORVERSION | VER_MINORVERSION |
            VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR |
            VER_PRODUCT_TYPE;

        // Perform the test.
        BOOL isVistaRTMorXP = VerifyVersionInfo(&osvi, dwTypeMask, dwlConditionMask);
        if (isVistaRTMorXP != 0) {
            RTC_LOG(LS_VERBOSE << "*** Windows Core Audio is only supported on Vista SP1 or later => will revert to the Wave API ***");
            return false;
        }

        // 2) Initializes the COM library for use by the calling thread.

        // The COM init wrapper sets the thread's concurrency model to MTA,
        // and creates a new apartment for the thread if one is required. The
        // wrapper also ensures that each call to CoInitializeEx is balanced
        // by a corresponding call to CoUninitialize.
        //
        ScopedCOMInitializer comInit(ScopedCOMInitializer::kMTA);
        if (!comInit.succeeded()) {
            // Things will work even if an STA thread is calling this method but we
            // want to ensure that MTA is used and therefore return false here.
            return false;
        }

        // 3) Check if the MMDevice API is available.
        //
        // The Windows Multimedia Device (MMDevice) API enables audio clients to
        // discover audio endpoint devices, determine their capabilities, and create
        // driver instances for those devices.
        // Header file Mmdeviceapi.h defines the interfaces in the MMDevice API.
        // The MMDevice API consists of several interfaces. The first of these is the
        // IMMDeviceEnumerator interface. To access the interfaces in the MMDevice
        // API, a client obtains a reference to the IMMDeviceEnumerator interface of a
        // device-enumerator object by calling the CoCreateInstance function.
        //
        // Through the IMMDeviceEnumerator interface, the client can obtain references
        // to the other interfaces in the MMDevice API. The MMDevice API implements
        // the following interfaces:
        //
        // IMMDevice            Represents an audio device.
        // IMMDeviceCollection  Represents a collection of audio devices.
        // IMMDeviceEnumerator  Provides methods for enumerating audio devices.
        // IMMEndpoint          Represents an audio endpoint device.
        //
        IMMDeviceEnumerator* pIMMD(NULL);
        const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
        const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);

        hr = CoCreateInstance(
            CLSID_MMDeviceEnumerator,  // GUID value of MMDeviceEnumerator coclass
            NULL, CLSCTX_ALL,
            IID_IMMDeviceEnumerator,  // GUID value of the IMMDeviceEnumerator
                                      // interface
            (void**)&pIMMD);

        if (FAILED(hr)) {
            RTC_LOG(LS_ERROR << "AudioDeviceWindowsCore::CoreAudioIsSupported() Failed to create the required COM object (hr=" << hr << ")");
            RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::CoreAudioIsSupported() CoCreateInstance(MMDeviceEnumerator) failed (hr=" << hr << ")");

            const DWORD dwFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            const DWORD dwLangID = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

            // Gets the system's human readable message string for this HRESULT.
            // All error message in English by default.
            DWORD messageLength = ::FormatMessageW(dwFlags, 0, hr, dwLangID, errorText, MAXERRORLENGTH, NULL);

            assert(messageLength <= MAXERRORLENGTH);

            // Trims tailing white space (FormatMessage() leaves a trailing cr-lf.).
            for (; messageLength && ::isspace(errorText[messageLength - 1]);
                --messageLength) {
                errorText[messageLength - 1] = '\0';
            }

            StringCchPrintf(buf, MAXERRORLENGTH, TEXT("Error details: "));
            StringCchCat(buf, MAXERRORLENGTH, errorText);
            RTC_LOG(LS_VERBOSE << buf);
        }
        else {
            MMDeviceIsAvailable = true;
            RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::CoreAudioIsSupported()" << " CoCreateInstance(MMDeviceEnumerator) succeeded (hr=" << hr << ")");
            SAFE_RELEASE(pIMMD);
        }

        // 4) Verify that we can create and initialize our Core Audio class.
        //
        // Also, perform a limited "API test" to ensure that Core Audio is supported
        // for all devices.
        //
        if (MMDeviceIsAvailable) {
            coreAudioIsSupported = false;

            AudioDeviceWindowsCore* p = new AudioDeviceWindowsCore();
            if (p == NULL) {
                return false;
            }

            int ok(0);
            int temp_ok(0);
            bool available(false);

            if (p->Init() != InitStatus::OK) {
                ok |= -1;
            }

            int16_t numDevsRec = p->RecordingDevices();
            for (uint16_t i = 0; i < numDevsRec; i++) {
                ok |= p->SetRecordingDevice(i);
                temp_ok = p->RecordingIsAvailable(available);
                ok |= temp_ok;
                ok |= (available == false);

                if (available) {
                    ok |= p->InitMicrophone();
                }

                if (ok)
                    RTC_LOG(LS_WARNING << "AudioDeviceWindowsCore::CoreAudioIsSupported() Failed to use Core Audio Recording for device id=" << i);
            }

            int16_t numDevsPlay = p->PlayoutDevices();
            for (uint16_t i = 0; i < numDevsPlay; i++) {
                ok |= p->SetPlayoutDevice(i);
                temp_ok = p->PlayoutIsAvailable(available);
                ok |= temp_ok;
                ok |= (available == false);

                if (available) {
                    ok |= p->InitSpeaker();
                }

                if (ok) {
                    RTC_LOG(LS_WARNING << "AudioDeviceWindowsCore::CoreAudioIsSupported() Failed to use Core Audio Playout for device id=" << i);
                }
            }

            ok |= p->Terminate();

            if (ok == 0) {
                coreAudioIsSupported = true;
            }

            delete p;
        }

        if (coreAudioIsSupported) {
            RTC_LOG(LS_VERBOSE << "*** Windows Core Audio is supported ***");
        }
        else {
            RTC_LOG(LS_VERBOSE << "*** Windows Core Audio is NOT supported" << " => will revert to the Wave API ***");
        }

        return (coreAudioIsSupported);
    }

    // ============================================================================
    //                            Construction & Destruction
    // ============================================================================

    // ----------------------------------------------------------------------------
    //  AudioDeviceWindowsCore() - ctor
    // ----------------------------------------------------------------------------

    AudioDeviceWindowsCore::AudioDeviceWindowsCore()
        : m_comInit(ScopedCOMInitializer::kMTA)
        , m_ptrAudioBuffer(NULL)
        , m_ptrEnumerator(NULL)
        , m_ptrRenderCollection(NULL)
        , m_ptrCaptureCollection(NULL)
        , m_ptrDeviceOut(NULL)
        , m_ptrDeviceIn(NULL)
        , m_ptrClientOut(NULL)
        , m_ptrClientIn(NULL)
        , m_ptrRenderClient(NULL)
        , m_ptrCaptureClient(NULL)
        , m_ptrCaptureVolume(NULL)
        , m_ptrRenderSimpleVolume(NULL)
        , m_dmo(NULL)
        , m_mediaBuffer(NULL)
        , m_builtInAecEnabled(false)
        , m_playAudioFrameSize(0)
        , m_playSampleRate(0)
        , m_playBlockSize(0)
        , m_playChannels(2)
        , m_sndCardPlayDelay(0)
        , m_sndCardRecDelay(0)
        , m_writtenSamples(0)
        , m_readSamples(0)
        , m_recAudioFrameSize(0)
        , m_recSampleRate(0)
        , m_recBlockSize(0)
        , m_recChannels(2)
        , _avrtLibrary(NULL)
        , _winSupportAvrt(false)
        , m_hRenderSamplesReadyEvent(NULL)
        , m_hPlayThread(NULL)
        , m_hCaptureSamplesReadyEvent(NULL)
        , m_hRecThread(NULL)
        , m_hShutdownRenderEvent(NULL)
        , m_hShutdownCaptureEvent(NULL)
        , m_hRenderStartedEvent(NULL)
        , m_hCaptureStartedEvent(NULL)
        , m_hMmTask(NULL)
        , m_initialized(false)
        , m_recording(false)
        , m_playing(false)
        , m_recIsInitialized(false)
        , m_playIsInitialized(false)
        , m_speakerIsInitialized(false)
        , m_microphoneIsInitialized(false)
        , m_playBufDelay(80)
        , m_usingInputDeviceIndex(false)
        , m_usingOutputDeviceIndex(false)
        , m_inputDevice(AudioDeviceModule::kDefaultCommunicationDevice)
        , m_outputDevice(AudioDeviceModule::kDefaultCommunicationDevice)
        , m_inputDeviceIndex(0)
        , m_outputDeviceIndex(0)
    {
        RTC_LOG(LS_INFO << __FUNCTION__ << " created");
        assert(m_comInit.succeeded());

        // Try to load the Avrt DLL
        if (!_avrtLibrary) {
            // Get handle to the Avrt DLL module.
            _avrtLibrary = LoadLibrary(TEXT("Avrt.dll"));
            if (_avrtLibrary) {
                // Handle is valid (should only happen if OS larger than vista & win7).
                // Try to get the function addresses.
                RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::AudioDeviceWindowsCore() The Avrt DLL module is now loaded");

                _PAvRevertMmThreadCharacteristics = (PAvRevertMmThreadCharacteristics)GetProcAddress(_avrtLibrary, "AvRevertMmThreadCharacteristics");
                _PAvSetMmThreadCharacteristicsA = (PAvSetMmThreadCharacteristicsA)GetProcAddress(_avrtLibrary, "AvSetMmThreadCharacteristicsA");
                _PAvSetMmThreadPriority = (PAvSetMmThreadPriority)GetProcAddress(_avrtLibrary, "AvSetMmThreadPriority");

                if (_PAvRevertMmThreadCharacteristics && _PAvSetMmThreadCharacteristicsA && _PAvSetMmThreadPriority) {
                    RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::AudioDeviceWindowsCore()" << " AvRevertMmThreadCharacteristics() is OK");
                    RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::AudioDeviceWindowsCore()" << " AvSetMmThreadCharacteristicsA() is OK");
                    RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::AudioDeviceWindowsCore()" << " AvSetMmThreadPriority() is OK");
                    _winSupportAvrt = true;
                }
            }
        }

        // Create our samples ready events - we want auto reset events that start in
        // the not-signaled state. The state of an auto-reset event object remains
        // signaled until a single waiting thread is released, at which time the
        // system automatically sets the state to nonsignaled. If no threads are
        // waiting, the event object's state remains signaled. (Except for
        // _hShutdownCaptureEvent, which is used to shutdown multiple threads).
        m_hRenderSamplesReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        m_hCaptureSamplesReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        m_hShutdownRenderEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        m_hShutdownCaptureEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        m_hRenderStartedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        m_hCaptureStartedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

        m_perfCounterFreq.QuadPart = 1;
        m_perfCounterFactor = 0.0;

        // list of number of channels to use on recording side
        m_recChannelsPrioList[0] = 2;  // stereo is prio 1
        m_recChannelsPrioList[1] = 1;  // mono is prio 2
        m_recChannelsPrioList[2] = 4;  // quad is prio 3

                                      // list of number of channels to use on playout side
        m_playChannelsPrioList[0] = 2;  // stereo is prio 1
        m_playChannelsPrioList[1] = 1;  // mono is prio 2

        HRESULT hr;

        // We know that this API will work since it has already been verified in
        // CoreAudioIsSupported, hence no need to check for errors here as well.

        // Retrive the IMMDeviceEnumerator API (should load the MMDevAPI.dll)
        // TODO(henrika): we should probably move this allocation to Init() instead
        // and deallocate in Terminate() to make the implementation more symmetric.
        CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&m_ptrEnumerator));
        assert(NULL != m_ptrEnumerator);

        // DMO initialization for built-in WASAPI AEC.
        {
            IMediaObject* ptrDMO = NULL;
            hr = CoCreateInstance(CLSID_CWMAudioAEC, NULL, CLSCTX_INPROC_SERVER, IID_IMediaObject, reinterpret_cast<void**>(&ptrDMO));
            if (FAILED(hr) || ptrDMO == NULL) {
                // Since we check that _dmo is non-NULL in EnableBuiltInAEC(), the
                // feature is prevented from being enabled.
                m_builtInAecEnabled = false;
                _TraceCOMError(hr);
            }
            m_dmo = ptrDMO;
            SAFE_RELEASE(ptrDMO);
        }
    }

    // ----------------------------------------------------------------------------
    //  AudioDeviceWindowsCore() - dtor
    // ----------------------------------------------------------------------------

    AudioDeviceWindowsCore::~AudioDeviceWindowsCore() {
        RTC_LOG(LS_INFO << __FUNCTION__ << " destroyed");

        Terminate();

        // The IMMDeviceEnumerator is created during construction. Must release
        // it here and not in Terminate() since we don't recreate it in Init().
        m_ptrEnumerator.Release();

        m_ptrAudioBuffer = NULL;

        if (NULL != m_hRenderSamplesReadyEvent) {
            CloseHandle(m_hRenderSamplesReadyEvent);
            m_hRenderSamplesReadyEvent = NULL;
        }

        if (NULL != m_hCaptureSamplesReadyEvent) {
            CloseHandle(m_hCaptureSamplesReadyEvent);
            m_hCaptureSamplesReadyEvent = NULL;
        }

        if (NULL != m_hRenderStartedEvent) {
            CloseHandle(m_hRenderStartedEvent);
            m_hRenderStartedEvent = NULL;
        }

        if (NULL != m_hCaptureStartedEvent) {
            CloseHandle(m_hCaptureStartedEvent);
            m_hCaptureStartedEvent = NULL;
        }

        if (NULL != m_hShutdownRenderEvent) {
            CloseHandle(m_hShutdownRenderEvent);
            m_hShutdownRenderEvent = NULL;
        }

        if (NULL != m_hShutdownCaptureEvent) {
            CloseHandle(m_hShutdownCaptureEvent);
            m_hShutdownCaptureEvent = NULL;
        }

        if (_avrtLibrary) 
        {
            BOOL freeOK = FreeLibrary(_avrtLibrary);
            if (!freeOK) {
                RTC_LOG(LS_WARNING << "AudioDeviceWindowsCore::~AudioDeviceWindowsCore() failed to free the loaded Avrt DLL module correctly");
            }
            else {
                RTC_LOG(LS_WARNING << "AudioDeviceWindowsCore::~AudioDeviceWindowsCore() the Avrt DLL module is now unloaded");
            }
        }
    }

    // ============================================================================
    //                                     API
    // ============================================================================

    // ----------------------------------------------------------------------------
    //  AttachAudioBuffer
    // ----------------------------------------------------------------------------

    void AudioDeviceWindowsCore::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {
        m_ptrAudioBuffer = audioBuffer;

        // Inform the AudioBuffer about default settings for this implementation.
        // Set all values to zero here since the actual settings will be done by
        // InitPlayout and InitRecording later.
        m_ptrAudioBuffer->SetRecordingSampleRate(0);
        m_ptrAudioBuffer->SetPlayoutSampleRate(0);
        m_ptrAudioBuffer->SetRecordingChannels(0);
        m_ptrAudioBuffer->SetPlayoutChannels(0);
    }

    // ----------------------------------------------------------------------------
    //  ActiveAudioLayer
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::ActiveAudioLayer(AudioDeviceModule::AudioLayer& audioLayer) const 
    {
        audioLayer = AudioDeviceModule::kWindowsCoreAudio;
        return 0;
    }

    // ----------------------------------------------------------------------------
    //  Init
    // ----------------------------------------------------------------------------

    AudioDeviceGeneric::InitStatus 
    AudioDeviceWindowsCore::Init() 
    {
        rtc::CritScope lock(&m_critSect);

        if (m_initialized)
            return InitStatus::OK;

        // Enumerate all audio rendering and capturing endpoint devices.
        // Note that, some of these will not be able to select by the user.
        // The complete collection is for internal use only.
//        _EnumerateEndpointDevicesAll(eRender);
//        _EnumerateEndpointDevicesAll(eCapture);

        m_initialized = true;

        return InitStatus::OK;
    }

    // ----------------------------------------------------------------------------
    //  Terminate
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::Terminate() {
        rtc::CritScope lock(&m_critSect);

        if (!m_initialized) {
            return 0;
        }

        m_initialized = false;
        m_speakerIsInitialized = false;
        m_microphoneIsInitialized = false;
        m_playing = false;
        m_recording = false;

        m_ptrRenderCollection.Release();
        m_ptrCaptureCollection.Release();
        m_ptrDeviceOut.Release();
        m_ptrDeviceIn.Release();
        m_ptrClientOut.Release();
        m_ptrClientIn.Release();
        m_ptrRenderClient.Release();
        m_ptrCaptureClient.Release();
        m_ptrCaptureVolume.Release();
        m_ptrRenderSimpleVolume.Release();

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  Initialized
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::Initialized() const {
        return (m_initialized);
    }

    // ----------------------------------------------------------------------------
    //  InitSpeaker
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::InitSpeaker() {
        rtc::CritScope lock(&m_critSect);

        if (m_playing)
            return -1;

        if (m_ptrDeviceOut == NULL)
            return -1;

        if (m_usingOutputDeviceIndex) {
            int16_t nDevices = PlayoutDevices();
            if (m_outputDeviceIndex > (nDevices - 1)) {
                RTC_LOG(LS_ERROR << "current device selection is invalid => unable to initialize");
                return -1;
            }
        }

        int32_t ret(0);

        m_ptrDeviceOut.Release();
        if (m_usingOutputDeviceIndex) {
            // Refresh the selected rendering endpoint device using current index
            ret = _GetListDevice(eRender, m_outputDeviceIndex, &m_ptrDeviceOut);
        }
        else {
            ERole role;
            (m_outputDevice == AudioDeviceModule::kDefaultDevice) ? role = eConsole : role = eCommunications;
            // Refresh the selected rendering endpoint device using role
            ret = _GetDefaultDevice(eRender, role, &m_ptrDeviceOut);
        }

        if (ret != 0 || (m_ptrDeviceOut == NULL)) {
            RTC_LOG(LS_ERROR << "failed to initialize the rendering enpoint device");
            m_ptrDeviceOut.Release();
            return -1;
        }

        CComPtr<IAudioSessionManager> pManager;
        ret = m_ptrDeviceOut->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, NULL, (void**)&pManager);
        if (ret != 0 || pManager == NULL) 
        {
            RTC_LOG(LS_ERROR << "failed to initialize the render manager");
            pManager.Release();
            return -1;
        }

        m_ptrRenderSimpleVolume.Release();
        ret = pManager->GetSimpleAudioVolume(NULL, FALSE, &m_ptrRenderSimpleVolume);
        if (ret != 0 || m_ptrRenderSimpleVolume == NULL) 
        {
            RTC_LOG(LS_ERROR << "failed to initialize the render simple volume");
            pManager.Release();
            m_ptrRenderSimpleVolume.Release();
            return -1;
        }

        pManager.Release();

        m_speakerIsInitialized = true;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  InitMicrophone
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::InitMicrophone() {
        rtc::CritScope lock(&m_critSect);

        if (m_recording) {
            return -1;
        }

        if (m_ptrDeviceIn == NULL) {
            return -1;
        }

        if (m_usingInputDeviceIndex) {
            int16_t nDevices = RecordingDevices();
            if (m_inputDeviceIndex > (nDevices - 1)) {
                RTC_LOG(LS_ERROR << "current device selection is invalid => unable to initialize");
                return -1;
            }
        }

        int32_t ret(0);

        m_ptrDeviceIn.Release();
        if (m_usingInputDeviceIndex) {
            // Refresh the selected capture endpoint device using current index
            ret = _GetListDevice(eCapture, m_inputDeviceIndex, &m_ptrDeviceIn);
        }
        else {
            ERole role;
            (m_inputDevice == AudioDeviceModule::kDefaultDevice) ? role = eConsole : role = eCommunications;
            // Refresh the selected capture endpoint device using role
            ret = _GetDefaultDevice(eCapture, role, &m_ptrDeviceIn);
        }

        if (ret != 0 || (m_ptrDeviceIn == NULL)) {
            RTC_LOG(LS_ERROR << "failed to initialize the capturing enpoint device");
            m_ptrDeviceIn.Release();
            return -1;
        }

        m_ptrCaptureVolume.Release();
        ret = m_ptrDeviceIn->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
            reinterpret_cast<void**>(&m_ptrCaptureVolume));
        if (ret != 0 || m_ptrCaptureVolume == NULL) {
            RTC_LOG(LS_ERROR << "failed to initialize the capture volume");
            m_ptrCaptureVolume.Release();
            return -1;
        }

        m_microphoneIsInitialized = true;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  SpeakerIsInitialized
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::SpeakerIsInitialized() const {
        return (m_speakerIsInitialized);
    }

    // ----------------------------------------------------------------------------
    //  MicrophoneIsInitialized
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::MicrophoneIsInitialized() const {
        return (m_microphoneIsInitialized);
    }

    // ----------------------------------------------------------------------------
    //  SpeakerVolumeIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SpeakerVolumeIsAvailable(bool& available) 
    {
        HRESULT hr = S_OK;

        while(true)
        {
            rtc::CritScope lock(&m_critSect);

            if (m_ptrDeviceOut == NULL)
                return -1;

            CComPtr<IAudioSessionManager> pManager;
            CComPtr<ISimpleAudioVolume> pVolume;

            hr = m_ptrDeviceOut->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, NULL, (void**)&pManager);
            if (FAILED(hr))
                break;

            hr = pManager->GetSimpleAudioVolume(NULL, FALSE, &pVolume);
            if (FAILED(hr))
                break;

            float volume(0.0f);
            hr = pVolume->GetMasterVolume(&volume);
            if (FAILED(hr))
                available = false;

            available = true;

            return 0;
        }

        _TraceCOMError(hr);

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SetSpeakerVolume
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetSpeakerVolume(uint32_t volume) {
        {
            rtc::CritScope lock(&m_critSect);

            if (!m_speakerIsInitialized) {
                return -1;
            }

            if (m_ptrDeviceOut == NULL) {
                return -1;
            }
        }

        if (volume < (uint32_t)MIN_CORE_SPEAKER_VOLUME ||
            volume >(uint32_t)MAX_CORE_SPEAKER_VOLUME) {
            return -1;
        }

        HRESULT hr = S_OK;

        // scale input volume to valid range (0.0 to 1.0)
        const float fLevel = (float)volume / MAX_CORE_SPEAKER_VOLUME;
        m_volumeMutex.Enter();
        hr = m_ptrRenderSimpleVolume->SetMasterVolume(fLevel, NULL);
        m_volumeMutex.Leave();
        EXIT_ON_ERROR(hr);

        return 0;

    Exit:
        _TraceCOMError(hr);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SpeakerVolume
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SpeakerVolume(uint32_t& volume) const {
        {
            rtc::CritScope lock(&m_critSect);

            if (!m_speakerIsInitialized) {
                return -1;
            }

            if (m_ptrDeviceOut == NULL) {
                return -1;
            }
        }

        HRESULT hr = S_OK;
        float fLevel(0.0f);

        m_volumeMutex.Enter();
        hr = m_ptrRenderSimpleVolume->GetMasterVolume(&fLevel);
        m_volumeMutex.Leave();
        EXIT_ON_ERROR(hr);

        // scale input volume range [0.0,1.0] to valid output range
        volume = static_cast<uint32_t>(fLevel * MAX_CORE_SPEAKER_VOLUME);

        return 0;

    Exit:
        _TraceCOMError(hr);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  MaxSpeakerVolume
    //
    //  The internal range for Core Audio is 0.0 to 1.0, where 0.0 indicates
    //  silence and 1.0 indicates full volume (no attenuation).
    //  We add our (webrtc-internal) own max level to match the Wave API and
    //  how it is used today in VoE.
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MaxSpeakerVolume(uint32_t& maxVolume) const {
        if (!m_speakerIsInitialized) {
            return -1;
        }

        maxVolume = static_cast<uint32_t>(MAX_CORE_SPEAKER_VOLUME);

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  MinSpeakerVolume
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MinSpeakerVolume(uint32_t& minVolume) const {
        if (!m_speakerIsInitialized) {
            return -1;
        }

        minVolume = static_cast<uint32_t>(MIN_CORE_SPEAKER_VOLUME);

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  SpeakerMuteIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SpeakerMuteIsAvailable(bool& available) {
        rtc::CritScope lock(&m_critSect);

        if (m_ptrDeviceOut == NULL) {
            return -1;
        }

        HRESULT hr = S_OK;
        IAudioEndpointVolume* pVolume = NULL;

        // Query the speaker system mute state.
        hr = m_ptrDeviceOut->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
            reinterpret_cast<void**>(&pVolume));
        EXIT_ON_ERROR(hr);

        BOOL mute;
        hr = pVolume->GetMute(&mute);
        if (FAILED(hr))
            available = false;
        else
            available = true;

        SAFE_RELEASE(pVolume);

        return 0;

    Exit:
        _TraceCOMError(hr);
        SAFE_RELEASE(pVolume);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SetSpeakerMute
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetSpeakerMute(bool enable) {
        HRESULT hr = S_OK;

        while(true){
            rtc::CritScope lock(&m_critSect);

            if (!m_speakerIsInitialized) {
                return -1;
            }

            if (m_ptrDeviceOut == NULL) {
                return -1;
            }

            CComPtr<IAudioEndpointVolume> pVolume;

            // Set the speaker system mute state.
            hr = m_ptrDeviceOut->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
                reinterpret_cast<void**>(&pVolume));
            if (FAILED(hr))
                break;

            const BOOL mute(enable);
            hr = pVolume->SetMute(mute, NULL);
            if (FAILED(hr))
                break;

            return 0;
        }

        _TraceCOMError(hr);

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SpeakerMute
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SpeakerMute(bool& enabled) const {
        if (!m_speakerIsInitialized) {
            return -1;
        }

        if (m_ptrDeviceOut == NULL) {
            return -1;
        }

        HRESULT hr = S_OK;
        IAudioEndpointVolume* pVolume = NULL;

        // Query the speaker system mute state.
        hr = m_ptrDeviceOut->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
            reinterpret_cast<void**>(&pVolume));
        EXIT_ON_ERROR(hr);

        BOOL mute;
        hr = pVolume->GetMute(&mute);
        EXIT_ON_ERROR(hr);

        enabled = (mute == TRUE) ? true : false;

        SAFE_RELEASE(pVolume);

        return 0;

    Exit:
        _TraceCOMError(hr);
        SAFE_RELEASE(pVolume);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  MicrophoneMuteIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MicrophoneMuteIsAvailable(bool& available) {
        rtc::CritScope lock(&m_critSect);

        if (m_ptrDeviceIn == NULL) {
            return -1;
        }

        HRESULT hr = S_OK;
        IAudioEndpointVolume* pVolume = NULL;

        // Query the microphone system mute state.
        hr = m_ptrDeviceIn->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
            reinterpret_cast<void**>(&pVolume));
        EXIT_ON_ERROR(hr);

        BOOL mute;
        hr = pVolume->GetMute(&mute);
        if (FAILED(hr))
            available = false;
        else
            available = true;

        SAFE_RELEASE(pVolume);
        return 0;

    Exit:
        _TraceCOMError(hr);
        SAFE_RELEASE(pVolume);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SetMicrophoneMute
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetMicrophoneMute(bool enable) {
        HRESULT hr = S_OK;

        while(true)
        {
            if (!m_microphoneIsInitialized)
                return -1;

            if (m_ptrDeviceIn == NULL)
                return -1;

            CComPtr<IAudioEndpointVolume> pVolume;

            // Set the microphone system mute state.
            hr = m_ptrDeviceIn->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
                reinterpret_cast<void**>(&pVolume));
            if (FAILED(hr))
                break;

            const BOOL mute(enable);
            hr = pVolume->SetMute(mute, NULL);
            if (FAILED(hr))
                break;

            return 0;
        }

        _TraceCOMError(hr);

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  MicrophoneMute
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MicrophoneMute(bool& enabled) const {
        if (!m_microphoneIsInitialized) {
            return -1;
        }

        HRESULT hr = S_OK;
        IAudioEndpointVolume* pVolume = NULL;

        // Query the microphone system mute state.
        hr = m_ptrDeviceIn->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
            reinterpret_cast<void**>(&pVolume));
        EXIT_ON_ERROR(hr);

        BOOL mute;
        hr = pVolume->GetMute(&mute);
        EXIT_ON_ERROR(hr);

        enabled = (mute == TRUE) ? true : false;

        SAFE_RELEASE(pVolume);
        return 0;

    Exit:
        _TraceCOMError(hr);
        SAFE_RELEASE(pVolume);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  StereoRecordingIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StereoRecordingIsAvailable(bool& available) {
        available = true;
        return 0;
    }

    // ----------------------------------------------------------------------------
    //  SetStereoRecording
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetStereoRecording(bool enable) {
        rtc::CritScope lock(&m_critSect);

        if (enable) {
            m_recChannelsPrioList[0] = 2;  // try stereo first
            m_recChannelsPrioList[1] = 1;
            m_recChannels = 2;
        }
        else {
            m_recChannelsPrioList[0] = 1;  // try mono first
            m_recChannelsPrioList[1] = 2;
            m_recChannels = 1;
        }

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  StereoRecording
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StereoRecording(bool& enabled) const {
        if (m_recChannels == 2)
            enabled = true;
        else
            enabled = false;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  StereoPlayoutIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StereoPlayoutIsAvailable(bool& available) {
        available = true;
        return 0;
    }

    // ----------------------------------------------------------------------------
    //  SetStereoPlayout
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetStereoPlayout(bool enable) {
        rtc::CritScope lock(&m_critSect);

        if (enable) {
            m_playChannelsPrioList[0] = 2;  // try stereo first
            m_playChannelsPrioList[1] = 1;
            m_playChannels = 2;
        }
        else {
            m_playChannelsPrioList[0] = 1;  // try mono first
            m_playChannelsPrioList[1] = 2;
            m_playChannels = 1;
        }

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  StereoPlayout
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StereoPlayout(bool& enabled) const {
        if (m_playChannels == 2)
            enabled = true;
        else
            enabled = false;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  MicrophoneVolumeIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MicrophoneVolumeIsAvailable(bool& available) {
        HRESULT hr = S_OK;
        while(true)
        {
            rtc::CritScope lock(&m_critSect);

            if (m_ptrDeviceIn == NULL)
                return -1;

            IAudioEndpointVolume* pVolume = NULL;

            hr = m_ptrDeviceIn->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, reinterpret_cast<void**>(&pVolume));
            if (FAILED(hr))                break;
            float volume(0.0f);
            available = SUCCEEDED(pVolume->GetMasterVolumeLevelScalar(&volume));

            return 0;
        }

        _TraceCOMError(hr);

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SetMicrophoneVolume
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetMicrophoneVolume(uint32_t volume) {
        RTC_LOG(LS_VERBOSE << "AudioDeviceWindowsCore::SetMicrophoneVolume(volume=" << volume << ")" );

        {
            rtc::CritScope lock(&m_critSect);

            if (!m_microphoneIsInitialized) {
                return -1;
            }

            if (m_ptrDeviceIn == NULL) {
                return -1;
            }
        }

        if (volume < static_cast<uint32_t>(MIN_CORE_MICROPHONE_VOLUME) ||
            volume > static_cast<uint32_t>(MAX_CORE_MICROPHONE_VOLUME)) {
            return -1;
        }

        HRESULT hr = S_OK;
        // scale input volume to valid range (0.0 to 1.0)
        const float fLevel = static_cast<float>(volume) / MAX_CORE_MICROPHONE_VOLUME;
        m_volumeMutex.Enter();
        m_ptrCaptureVolume->SetMasterVolumeLevelScalar(fLevel, NULL);
        m_volumeMutex.Leave();
        EXIT_ON_ERROR(hr);

        return 0;

    Exit:
        _TraceCOMError(hr);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  MicrophoneVolume
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MicrophoneVolume(uint32_t& volume) const {
        {
            rtc::CritScope lock(&m_critSect);

            if (!m_microphoneIsInitialized) {
                return -1;
            }

            if (m_ptrDeviceIn == NULL) {
                return -1;
            }
        }

        HRESULT hr = S_OK;
        float fLevel(0.0f);
        volume = 0;
        m_volumeMutex.Enter();
        hr = m_ptrCaptureVolume->GetMasterVolumeLevelScalar(&fLevel);
        m_volumeMutex.Leave();
        EXIT_ON_ERROR(hr);

        // scale input volume range [0.0,1.0] to valid output range
        volume = static_cast<uint32_t>(fLevel * MAX_CORE_MICROPHONE_VOLUME);

        return 0;

    Exit:
        _TraceCOMError(hr);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  MaxMicrophoneVolume
    //
    //  The internal range for Core Audio is 0.0 to 1.0, where 0.0 indicates
    //  silence and 1.0 indicates full volume (no attenuation).
    //  We add our (webrtc-internal) own max level to match the Wave API and
    //  how it is used today in VoE.
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MaxMicrophoneVolume(uint32_t& maxVolume) const {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        if (!m_microphoneIsInitialized) {
            return -1;
        }

        maxVolume = static_cast<uint32_t>(MAX_CORE_MICROPHONE_VOLUME);

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  MinMicrophoneVolume
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::MinMicrophoneVolume(uint32_t& minVolume) const {
        if (!m_microphoneIsInitialized) {
            return -1;
        }

        minVolume = static_cast<uint32_t>(MIN_CORE_MICROPHONE_VOLUME);

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  PlayoutDevices
    // ----------------------------------------------------------------------------

    int16_t AudioDeviceWindowsCore::PlayoutDevices() {
        rtc::CritScope lock(&m_critSect);

        if (_RefreshDeviceList(eRender) != -1) {
            return (_DeviceListCount(eRender));
        }

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SetPlayoutDevice I (II)
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetPlayoutDevice(uint16_t index) {
        if (m_playIsInitialized) {
            return -1;
        }

        // Get current number of available rendering endpoint devices and refresh the
        // rendering collection.
        UINT nDevices = PlayoutDevices();

        if (index < 0 || index >(nDevices - 1)) {
            RTC_LOG(LS_ERROR << "device index is out of range [0," << (nDevices - 1) << "]");
            return -1;
        }

        rtc::CritScope lock(&m_critSect);

        HRESULT hr(S_OK);

        assert(m_ptrRenderCollection != NULL);

        //  Select an endpoint rendering device given the specified index
        m_ptrDeviceOut.Release();
        hr = m_ptrRenderCollection->Item(index, &m_ptrDeviceOut);
        if (FAILED(hr)) {
            _TraceCOMError(hr);
            m_ptrDeviceOut.Release();
            return -1;
        }

        WCHAR szDeviceName[MAX_PATH];
        const int bufferLen = sizeof(szDeviceName) / sizeof(szDeviceName)[0];

        // Get the endpoint device's friendly-name
        if (_GetDeviceName(m_ptrDeviceOut, szDeviceName, bufferLen) == 0) {
            RTC_LOG(LS_VERBOSE << "friendly name: \"" << szDeviceName << "\"");
        }

        m_usingOutputDeviceIndex = true;
        m_outputDeviceIndex = index;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  SetPlayoutDevice II (II)
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetPlayoutDevice(AudioDeviceModule::WindowsDeviceType device) 
    {
        if (m_playIsInitialized)
            return -1;

        ERole role(eCommunications);

        if (device == AudioDeviceModule::kDefaultDevice)
            role = eConsole;
        else if (device == AudioDeviceModule::kDefaultCommunicationDevice)
            role = eCommunications;

        rtc::CritScope lock(&m_critSect);

        // Refresh the list of rendering endpoint devices
        _RefreshDeviceList(eRender);

        HRESULT hr(S_OK);

        assert(m_ptrEnumerator != NULL);

        //  Select an endpoint rendering device given the specified role
        m_ptrDeviceOut.Release();
        hr = m_ptrEnumerator->GetDefaultAudioEndpoint(eRender, role, &m_ptrDeviceOut);
        if (FAILED(hr)) 
        {
            _TraceCOMError(hr);
            m_ptrDeviceOut.Release();
            return -1;
        }

        WCHAR szDeviceName[MAX_PATH];
        const int bufferLen = sizeof(szDeviceName) / sizeof(szDeviceName)[0];

        // Get the endpoint device's friendly-name
        if (_GetDeviceName(m_ptrDeviceOut, szDeviceName, bufferLen) == 0) 
            RTC_LOG(LS_VERBOSE << "friendly name: \"" << szDeviceName << "\"");

        m_usingOutputDeviceIndex = false;
        m_outputDevice = device;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  PlayoutDeviceName
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::PlayoutDeviceName(const uint16_t index, std::string& name, std::string& guid)
    { 
        name.clear();
        guid.clear();

        bool defaultCommunicationDevice(false);
        const int16_t nDevices(PlayoutDevices());  // also updates the list of devices

                                                   // Special fix for the case when the user selects '-1' as index (<=> Default
                                                   // Communication Device)
        if (index == (uint16_t)(-1)) {
            defaultCommunicationDevice = true;
            RTC_LOG(LS_VERBOSE << "Default Communication endpoint device will be used");
        }

        if ((defaultCommunicationDevice ? 0 : index) > (nDevices - 1))
            return -1;

        rtc::CritScope lock(&m_critSect);

        int32_t ret(-1);
        WCHAR szDeviceName[MAX_PATH];
        const int bufferLen = sizeof(szDeviceName) / sizeof(szDeviceName)[0];

        // Get the endpoint device's friendly-name
        ret = defaultCommunicationDevice ? 
            _GetDefaultDeviceName(eRender, eCommunications, szDeviceName, bufferLen) : 
            _GetListDeviceName(eRender, index, szDeviceName, bufferLen);

        if (ret == 0) 
        {
            // Convert the endpoint device's friendly-name to UTF-8
            char _name[MAX_PATH]{ 0 };
            if (WideCharToMultiByte(CP_UTF8, 0, szDeviceName, -1, _name, MAX_PATH, NULL, NULL) == 0)
                RTC_LOG(LS_ERROR << "WideCharToMultiByte(CP_UTF8) failed with error code " << GetLastError());
            name = _name;
        }

        // Get the endpoint ID string (uniquely identifies the device among all audio
        // endpoint devices)
        ret = defaultCommunicationDevice ? 
            _GetDefaultDeviceID(eRender, eCommunications, szDeviceName, bufferLen) : 
            _GetListDeviceID(eRender, index, szDeviceName, bufferLen);

        if (ret == 0) {
            // Convert the endpoint device's ID string to UTF-8
            char _guid[MAX_PATH]{ 0 };
            if (WideCharToMultiByte(CP_UTF8, 0, szDeviceName, -1, _guid, MAX_PATH, NULL, NULL) == 0)
                RTC_LOG(LS_ERROR << "WideCharToMultiByte(CP_UTF8) failed with error code " << GetLastError());
            guid = _guid;
        }

        return ret;
    }

    // ----------------------------------------------------------------------------
    //  RecordingDeviceName
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::RecordingDeviceName(uint16_t index, char name[kAdmMaxDeviceNameSize], char guid[kAdmMaxGuidSize])
    {
        bool defaultCommunicationDevice(false);
        const int16_t nDevices(
            RecordingDevices());  // also updates the list of devices

                                  // Special fix for the case when the user selects '-1' as index (<=> Default
                                  // Communication Device)
        if (index == (uint16_t)(-1)) {
            defaultCommunicationDevice = true;
            index = 0;
            RTC_LOG(LS_VERBOSE << "Default Communication endpoint device will be used");
        }

        if ((index > (nDevices - 1)) || (name == NULL)) {
            return -1;
        }

        memset(name, 0, kAdmMaxDeviceNameSize);

        if (guid != NULL) {
            memset(guid, 0, kAdmMaxGuidSize);
        }

        rtc::CritScope lock(&m_critSect);

        int32_t ret(-1);
        WCHAR szDeviceName[MAX_PATH];
        const int bufferLen = sizeof(szDeviceName) / sizeof(szDeviceName)[0];

        // Get the endpoint device's friendly-name
        if (defaultCommunicationDevice) {
            ret = _GetDefaultDeviceName(eCapture, eCommunications, szDeviceName,
                bufferLen);
        }
        else {
            ret = _GetListDeviceName(eCapture, index, szDeviceName, bufferLen);
        }

        if (ret == 0) {
            // Convert the endpoint device's friendly-name to UTF-8
            if (WideCharToMultiByte(CP_UTF8, 0, szDeviceName, -1, name,
                kAdmMaxDeviceNameSize, NULL, NULL) == 0) {
                RTC_LOG(LS_ERROR << "WideCharToMultiByte(CP_UTF8) failed with error code " << GetLastError());
            }
        }

        // Get the endpoint ID string (uniquely identifies the device among all audio
        // endpoint devices)
        if (defaultCommunicationDevice) {
            ret =
                _GetDefaultDeviceID(eCapture, eCommunications, szDeviceName, bufferLen);
        }
        else {
            ret = _GetListDeviceID(eCapture, index, szDeviceName, bufferLen);
        }

        if (guid != NULL && ret == 0) {
            // Convert the endpoint device's ID string to UTF-8
            if (WideCharToMultiByte(CP_UTF8, 0, szDeviceName, -1, guid, kAdmMaxGuidSize, NULL, NULL) == 0) {
                RTC_LOG(LS_ERROR << "WideCharToMultiByte(CP_UTF8) failed with error code " << GetLastError());
            }
        }

        return ret;
    }

    // ----------------------------------------------------------------------------
    //  RecordingDevices
    // ----------------------------------------------------------------------------

    int16_t AudioDeviceWindowsCore::RecordingDevices() {
        rtc::CritScope lock(&m_critSect);

        if (_RefreshDeviceList(eCapture) != -1) {
            return (_DeviceListCount(eCapture));
        }

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  SetRecordingDevice I (II)
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetRecordingDevice(uint16_t index) {
        if (m_recIsInitialized) {
            return -1;
        }

        // Get current number of available capture endpoint devices and refresh the
        // capture collection.
        UINT nDevices = RecordingDevices();

        if (index < 0 || index >(nDevices - 1)) {
            RTC_LOG(LS_ERROR << "device index is out of range [0," << (nDevices - 1) << "]");
            return -1;
        }

        rtc::CritScope lock(&m_critSect);

        HRESULT hr(S_OK);

        assert(m_ptrCaptureCollection != NULL);

        // Select an endpoint capture device given the specified index
        m_ptrDeviceIn.Release();
        hr = m_ptrCaptureCollection->Item(index, &m_ptrDeviceIn);
        if (FAILED(hr)) {
            _TraceCOMError(hr);
            m_ptrDeviceIn.Release();
            return -1;
        }

        WCHAR szDeviceName[MAX_PATH];
        const int bufferLen = sizeof(szDeviceName) / sizeof(szDeviceName)[0];

        // Get the endpoint device's friendly-name
        if (_GetDeviceName(m_ptrDeviceIn, szDeviceName, bufferLen) == 0) {
            RTC_LOG(LS_VERBOSE << "friendly name: \"" << szDeviceName << "\"");
        }

        m_usingInputDeviceIndex = true;
        m_inputDeviceIndex = index;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  SetRecordingDevice II (II)
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::SetRecordingDevice(AudioDeviceModule::WindowsDeviceType device) 
    {
        if (m_recIsInitialized)
            return -1;

        ERole role(eCommunications);

        if (device == AudioDeviceModule::kDefaultDevice)
            role = eConsole;
        else if (device == AudioDeviceModule::kDefaultCommunicationDevice)
            role = eCommunications;

        rtc::CritScope lock(&m_critSect);

        // Refresh the list of capture endpoint devices
        _RefreshDeviceList(eCapture);

        HRESULT hr(S_OK);

        assert(m_ptrEnumerator != NULL);

        //  Select an endpoint capture device given the specified role
        m_ptrDeviceIn.Release();
        hr = m_ptrEnumerator->GetDefaultAudioEndpoint(eCapture, role, &m_ptrDeviceIn);
        
        if (FAILED(hr)) 
        {
            _TraceCOMError(hr);
            m_ptrDeviceIn.Release();
            return -1;
        }

        WCHAR szDeviceName[MAX_PATH];
        const int bufferLen = sizeof(szDeviceName) / sizeof(szDeviceName)[0];

        // Get the endpoint device's friendly-name
        if (_GetDeviceName(m_ptrDeviceIn, szDeviceName, bufferLen) == 0)
            RTC_LOG(LS_VERBOSE << "friendly name: \"" << szDeviceName << "\"");

        m_usingInputDeviceIndex = false;
        m_inputDevice = device;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  PlayoutIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::PlayoutIsAvailable(bool& available) 
    {
        available = false;

        // Try to initialize the playout side
        int32_t res = InitPlayout();

        // Cancel effect of initialization
        StopPlayout();

        if (res != -1)
            available = true;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  RecordingIsAvailable
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::RecordingIsAvailable(bool& available) 
    {
        available = false;

        // Try to initialize the recording side
        int32_t res = InitRecording();

        // Cancel effect of initialization
        StopRecording();

        if (res != -1)
            available = true;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  InitPlayout
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::InitPlayout() 
    {
        HRESULT hr = S_OK;
        WAVEFORMATEX* pWfxOut = NULL;
        WAVEFORMATEX* pWfxClosestMatch = NULL;

        while(true)
        {
            rtc::CritScope lock(&m_critSect);

            if (m_playing)
                return -1;

            if (m_playIsInitialized)
                return 0;

            if (m_ptrDeviceOut == NULL)
                return -1;

            // Initialize the speaker (devices might have been added or removed)
            if (InitSpeaker() == -1)
                RTC_LOG(LS_WARNING << "InitSpeaker() failed");

            // Ensure that the updated rendering endpoint device is valid
            if (m_ptrDeviceOut == NULL)
                return -1;

            if (m_builtInAecEnabled && m_recIsInitialized)
            {
                // Ensure the correct render device is configured in case
                // InitRecording() was called before InitPlayout().
                if (SetDMOProperties() == -1)
                    return -1;
            }

            WAVEFORMATEX Wfx = WAVEFORMATEX();

            // Create COM object with IAudioClient interface.
            m_ptrClientOut.Release();
            hr = m_ptrDeviceOut->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_ptrClientOut);
            if (FAILED(hr))
                break;

            // Retrieve the stream format that the audio engine uses for its internal
            // processing (mixing) of shared-mode streams.
            hr = m_ptrClientOut->GetMixFormat(&pWfxOut);
            if (SUCCEEDED(hr)) 
            {
                RTC_LOG(LS_VERBOSE << "Audio Engine's current rendering mix format:");
                // format type
                RTC_LOG(LS_VERBOSE << "wFormatTag     : 0x" << std::hex << pWfxOut->wFormatTag << std::dec << " (" << pWfxOut->wFormatTag << ")");
                // number of channels (i.e. mono, stereo...)
                RTC_LOG(LS_VERBOSE << "nChannels      : " << pWfxOut->nChannels);
                // sample rate
                RTC_LOG(LS_VERBOSE << "nSamplesPerSec : " << pWfxOut->nSamplesPerSec);
                // for buffer estimation
                RTC_LOG(LS_VERBOSE << "nAvgBytesPerSec: " << pWfxOut->nAvgBytesPerSec);
                // block size of data
                RTC_LOG(LS_VERBOSE << "nBlockAlign    : " << pWfxOut->nBlockAlign);
                // number of bits per sample of mono data
                RTC_LOG(LS_VERBOSE << "wBitsPerSample : " << pWfxOut->wBitsPerSample);
                RTC_LOG(LS_VERBOSE << "cbSize         : " << pWfxOut->cbSize);
            }

            // Set wave format
            Wfx.wFormatTag = WAVE_FORMAT_PCM;
            Wfx.wBitsPerSample = 16;
            Wfx.cbSize = 0;

            const int freqs[] = { 48000, 44100, 16000, 96000, 32000, 8000 };
            hr = S_FALSE;

            // Iterate over frequencies and channels, in order of priority
            for (unsigned int freq = 0; freq < sizeof(freqs) / sizeof(freqs[0]); freq++) {
                for (unsigned int chan = 0; chan < sizeof(m_playChannelsPrioList) / sizeof(m_playChannelsPrioList[0]); chan++) {
                    Wfx.nChannels = m_playChannelsPrioList[chan];
                    Wfx.nSamplesPerSec = freqs[freq];
                    Wfx.nBlockAlign = Wfx.nChannels * Wfx.wBitsPerSample / 8;
                    Wfx.nAvgBytesPerSec = Wfx.nSamplesPerSec * Wfx.nBlockAlign;
                    // If the method succeeds and the audio endpoint device supports the
                    // specified stream format, it returns S_OK. If the method succeeds and
                    // provides a closest match to the specified format, it returns S_FALSE.
                    hr = m_ptrClientOut->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &Wfx, &pWfxClosestMatch);
                    if (hr == S_OK) 
                    {
                        break;
                    }
                    else 
                    {
                        if (pWfxClosestMatch) {
                            RTC_LOG(LS_INFO  << "nChannels=" << Wfx.nChannels
                                             << ", nSamplesPerSec=" << Wfx.nSamplesPerSec
                                             << " is not supported. Closest match: "
                                             << "nChannels=" << pWfxClosestMatch->nChannels
                                             << ", nSamplesPerSec=" << pWfxClosestMatch->nSamplesPerSec);
                            CoTaskMemFree(pWfxClosestMatch);
                            pWfxClosestMatch = NULL;
                        }
                        else {
                            RTC_LOG(LS_INFO  << "nChannels=" << Wfx.nChannels 
                                             << ", nSamplesPerSec=" << Wfx.nSamplesPerSec 
                                             << " is not supported. No closest match.");
                        }
                    }
                }
                
                if (hr == S_OK)
                    break;
            }

            // TODO(andrew): what happens in the event of failure in the above loop?
            //   Is _ptrClientOut->Initialize expected to fail?
            //   Same in InitRecording().
            if (hr == S_OK) {
                m_playAudioFrameSize = Wfx.nBlockAlign;
                // Block size is the number of samples each channel in 10ms.
                m_playBlockSize = Wfx.nSamplesPerSec / 100;
                m_playSampleRate = Wfx.nSamplesPerSec;
                m_devicePlaySampleRate = Wfx.nSamplesPerSec;  // The device itself continues to run at 44.1 kHz.
                m_devicePlayBlockSize = Wfx.nSamplesPerSec / 100;
                m_playChannels = Wfx.nChannels;

                RTC_LOG(LS_VERBOSE << "VoE selected this rendering format:");
                RTC_LOG(LS_VERBOSE << "wFormatTag         : 0x" << std::hex << Wfx.wFormatTag << std::dec << " (" << Wfx.wFormatTag << ")");
                RTC_LOG(LS_VERBOSE << "nChannels          : " << Wfx.nChannels);
                RTC_LOG(LS_VERBOSE << "nSamplesPerSec     : " << Wfx.nSamplesPerSec);
                RTC_LOG(LS_VERBOSE << "nAvgBytesPerSec    : " << Wfx.nAvgBytesPerSec);
                RTC_LOG(LS_VERBOSE << "nBlockAlign        : " << Wfx.nBlockAlign);
                RTC_LOG(LS_VERBOSE << "wBitsPerSample     : " << Wfx.wBitsPerSample);
                RTC_LOG(LS_VERBOSE << "cbSize             : " << Wfx.cbSize);
                RTC_LOG(LS_VERBOSE << "Additional settings:");
                RTC_LOG(LS_VERBOSE << "_playAudioFrameSize: " << m_playAudioFrameSize);
                RTC_LOG(LS_VERBOSE << "_playBlockSize     : " << m_playBlockSize);
                RTC_LOG(LS_VERBOSE << "_playChannels      : " << m_playChannels);
            }

            // Create a rendering stream.
            //
            // ****************************************************************************
            // For a shared-mode stream that uses event-driven buffering, the caller must
            // set both hnsPeriodicity and hnsBufferDuration to 0. The Initialize method
            // determines how large a buffer to allocate based on the scheduling period
            // of the audio engine. Although the client's buffer processing thread is
            // event driven, the basic buffer management process, as described previously,
            // is unaltered.
            // Each time the thread awakens, it should call
            // IAudioClient::GetCurrentPadding to determine how much data to write to a
            // rendering buffer or read from a capture buffer. In contrast to the two
            // buffers that the Initialize method allocates for an exclusive-mode stream
            // that uses event-driven buffering, a shared-mode stream requires a single
            // buffer.
            // ****************************************************************************
            //
            REFERENCE_TIME hnsBufferDuration = 0;  // ask for minimum buffer size (default)
            if (m_devicePlaySampleRate == 44100) {
                // Ask for a larger buffer size (30ms) when using 44.1kHz as render rate.
                // There seems to be a larger risk of underruns for 44.1 compared
                // with the default rate (48kHz). When using default, we set the requested
                // buffer duration to 0, which sets the buffer to the minimum size
                // required by the engine thread. The actual buffer size can then be
                // read by GetBufferSize() and it is 20ms on most machines.
                hnsBufferDuration = 30 * 10000;
            }

            hr = m_ptrClientOut->Initialize(
                AUDCLNT_SHAREMODE_SHARED,  // share Audio Engine with other applications
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,  // processing of the audio buffer by
                                                    // the client will be event driven
                hnsBufferDuration,  // requested buffer capacity as a time value (in
                                    // 100-nanosecond units)
                0,                  // periodicity
                &Wfx,               // selected wave format
                NULL);              // session GUID

            if (FAILED(hr))
                RTC_LOG(LS_ERROR << "IAudioClient::Initialize() failed:");
            
            if (FAILED(hr))
                break;

            if (m_ptrAudioBuffer) {
                // Update the audio buffer with the selected parameters
                m_ptrAudioBuffer->SetPlayoutSampleRate(m_playSampleRate);
                m_ptrAudioBuffer->SetPlayoutChannels((uint8_t)m_playChannels);
            }
            else {
                // We can enter this state during CoreAudioIsSupported() when no
                // AudioDeviceImplementation has been created, hence the AudioDeviceBuffer
                // does not exist. It is OK to end up here since we don't initiate any media
                // in CoreAudioIsSupported().
                RTC_LOG(LS_VERBOSE << "AudioDeviceBuffer must be attached before streaming can start");
            }

            // Get the actual size of the shared (endpoint buffer).
            // Typical value is 960 audio frames <=> 20ms @ 48kHz sample rate.
            UINT bufferFrameCount(0);
            hr = m_ptrClientOut->GetBufferSize(&bufferFrameCount);
            if (SUCCEEDED(hr))
                RTC_LOG(LS_VERBOSE << "IAudioClient::GetBufferSize() => " << bufferFrameCount << " (<=> " << bufferFrameCount * m_playAudioFrameSize << " bytes)");

            // Set the event handle that the system signals when an audio buffer is ready
            // to be processed by the client.
            hr = m_ptrClientOut->SetEventHandle(m_hRenderSamplesReadyEvent);
            if (FAILED(hr))
                break;

            // Get an IAudioRenderClient interface.
            m_ptrRenderClient.Release();
            hr = m_ptrClientOut->GetService(__uuidof(IAudioRenderClient), (void**)&m_ptrRenderClient);
            if (FAILED(hr))
                break;

            // Mark playout side as initialized
            m_playIsInitialized = true;

            CoTaskMemFree(pWfxOut);
            CoTaskMemFree(pWfxClosestMatch);

            RTC_LOG(LS_VERBOSE << "render side is now initialized");

            return 0;
        }

        _TraceCOMError(hr);
        CoTaskMemFree(pWfxOut);
        CoTaskMemFree(pWfxClosestMatch);
        
        m_ptrClientOut.Release();
        m_ptrRenderClient.Release();
        return -1;
    }

    // Capture initialization when the built-in AEC DirectX Media Object (DMO) is
    // used. Called from InitRecording(), most of which is skipped over. The DMO
    // handles device initialization itself.
    // Reference: http://msdn.microsoft.com/en-us/library/ff819492(v=vs.85).aspx
    int32_t AudioDeviceWindowsCore::InitRecordingDMO() 
    {
        assert(m_builtInAecEnabled);
        assert(m_dmo != NULL);

        if (SetDMOProperties() == -1) 
            return -1;

        DMO_MEDIA_TYPE mt = { 0 };
        HRESULT hr = MoInitMediaType(&mt, sizeof(WAVEFORMATEX));
        if (FAILED(hr)) 
        {
            MoFreeMediaType(&mt);
            _TraceCOMError(hr);
            return -1;
        }

        mt.majortype = MEDIATYPE_Audio;
        mt.subtype = MEDIASUBTYPE_PCM;
        mt.formattype = FORMAT_WaveFormatEx;

        // Supported formats
        // nChannels: 1 (in AEC-only mode)
        // nSamplesPerSec: 8000, 11025, 16000, 22050
        // wBitsPerSample: 16
        WAVEFORMATEX* ptrWav = reinterpret_cast<WAVEFORMATEX*>(mt.pbFormat);
        ptrWav->wFormatTag = WAVE_FORMAT_PCM;
        ptrWav->nChannels = 1;
        // 16000 is the highest we can support with our resampler.
        ptrWav->nSamplesPerSec = 16000;
        ptrWav->nAvgBytesPerSec = 32000;
        ptrWav->nBlockAlign = 2;
        ptrWav->wBitsPerSample = 16;
        ptrWav->cbSize = 0;

        // Set the VoE format equal to the AEC output format.
        m_recAudioFrameSize = ptrWav->nBlockAlign;
        m_recSampleRate = ptrWav->nSamplesPerSec;
        m_recBlockSize = ptrWav->nSamplesPerSec / 100;
        m_recChannels = ptrWav->nChannels;

        // Set the DMO output format parameters.
        hr = m_dmo->SetOutputType(kAecCaptureStreamIndex, &mt, 0);
        MoFreeMediaType(&mt);
        if (FAILED(hr)) 
        {
            _TraceCOMError(hr);
            return -1;
        }

        if (m_ptrAudioBuffer) 
        {
            m_ptrAudioBuffer->SetRecordingSampleRate(m_recSampleRate);
            m_ptrAudioBuffer->SetRecordingChannels(m_recChannels);
        }
        else 
        {
            // Refer to InitRecording() for comments.
            RTC_LOG(LS_VERBOSE << "AudioDeviceBuffer must be attached before streaming can start");
        }

        m_mediaBuffer = new MediaBufferImpl(m_recBlockSize * m_recAudioFrameSize);

        // Optional, but if called, must be after media types are set.
        hr = m_dmo->AllocateStreamingResources();
        if (FAILED(hr)) 
        {
            _TraceCOMError(hr);
            return -1;
        }

        m_recIsInitialized = true;
        RTC_LOG(LS_VERBOSE << "Capture side is now initialized");

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  InitRecording
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::InitRecording() 
    {
        HRESULT hr = S_OK;
        WAVEFORMATEX* pWfxIn = NULL;
        WAVEFORMATEX* pWfxClosestMatch = NULL;

        while(true)
        {
            rtc::CritScope lock(&m_critSect);

            if (m_recording) {
                return -1;
            }

            if (m_recIsInitialized) {
                return 0;
            }

            if (QueryPerformanceFrequency(&m_perfCounterFreq) == 0) {
                return -1;
            }
            m_perfCounterFactor = 10000000.0 / (double)m_perfCounterFreq.QuadPart;

            if (m_ptrDeviceIn == NULL) {
                return -1;
            }

            // Initialize the microphone (devices might have been added or removed)
            if (InitMicrophone() == -1) {
                RTC_LOG(LS_WARNING << "InitMicrophone() failed");
            }

            // Ensure that the updated capturing endpoint device is valid
            if (m_ptrDeviceIn == NULL) {
                return -1;
            }

            if (m_builtInAecEnabled) {
                // The DMO will configure the capture device.
                return InitRecordingDMO();
            }

            WAVEFORMATEXTENSIBLE Wfx = WAVEFORMATEXTENSIBLE();

            // Create COM object with IAudioClient interface.
            m_ptrClientIn.Release();
            hr = m_ptrDeviceIn->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_ptrClientIn);
            if (FAILED(hr))
                break;

            // Retrieve the stream format that the audio engine uses for its internal
            // processing (mixing) of shared-mode streams.
            hr = m_ptrClientIn->GetMixFormat(&pWfxIn);
            if (SUCCEEDED(hr)) 
            {
                RTC_LOG(LS_VERBOSE << "Audio Engine's current capturing mix format:");
                // format type
                RTC_LOG(LS_VERBOSE << "wFormatTag     : 0x" << std::hex << pWfxIn->wFormatTag << std::dec << " (" << pWfxIn->wFormatTag << ")");
                // number of channels (i.e. mono, stereo...)
                RTC_LOG(LS_VERBOSE << "nChannels      : " << pWfxIn->nChannels);
                // sample rate
                RTC_LOG(LS_VERBOSE << "nSamplesPerSec : " << pWfxIn->nSamplesPerSec);
                // for buffer estimation
                RTC_LOG(LS_VERBOSE << "nAvgBytesPerSec: " << pWfxIn->nAvgBytesPerSec);
                // block size of data
                RTC_LOG(LS_VERBOSE << "nBlockAlign    : " << pWfxIn->nBlockAlign);
                // number of bits per sample of mono data
                RTC_LOG(LS_VERBOSE << "wBitsPerSample : " << pWfxIn->wBitsPerSample);
                RTC_LOG(LS_VERBOSE << "cbSize         : " << pWfxIn->cbSize);
            }

            // Set wave format
            Wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            Wfx.Format.wBitsPerSample = 16;
            Wfx.Format.cbSize = 22;
            Wfx.dwChannelMask = 0;
            Wfx.Samples.wValidBitsPerSample = Wfx.Format.wBitsPerSample;
            Wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

            const int freqs[6] = { 48000, 44100, 16000, 96000, 32000, 8000 };
            hr = S_FALSE;

            // Iterate over frequencies and channels, in order of priority
            for (unsigned int freq = 0; freq < sizeof(freqs) / sizeof(freqs[0]); freq++) 
            {
                for (unsigned int chan = 0; chan < sizeof(m_recChannelsPrioList) / sizeof(m_recChannelsPrioList[0]); chan++) 
                {
                    Wfx.Format.nChannels        = m_recChannelsPrioList[chan];
                    Wfx.Format.nSamplesPerSec   = freqs[freq];
                    Wfx.Format.nBlockAlign      = Wfx.Format.nChannels * Wfx.Format.wBitsPerSample / 8;
                    Wfx.Format.nAvgBytesPerSec  = Wfx.Format.nSamplesPerSec * Wfx.Format.nBlockAlign;

                    // If the method succeeds and the audio endpoint device supports the
                    // specified stream format, it returns S_OK. If the method succeeds and
                    // provides a closest match to the specified format, it returns S_FALSE.
                    hr = m_ptrClientIn->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, (WAVEFORMATEX*)&Wfx, &pWfxClosestMatch);
                    if (hr == S_OK) 
                    {
                        break;
                    }
                    else 
                    {
                        if (pWfxClosestMatch) 
                        {
                            RTC_LOG(LS_INFO << "nChannels=" << Wfx.Format.nChannels 
                                            << ", nSamplesPerSec=" << Wfx.Format.nSamplesPerSec
                                            << " is not supported. Closest match: "
                                            << "nChannels=" << pWfxClosestMatch->nChannels
                                            << ", nSamplesPerSec="
                                            << pWfxClosestMatch->nSamplesPerSec
                            );

                            CoTaskMemFree(pWfxClosestMatch);
                            pWfxClosestMatch = NULL;
                        }
                        else 
                        {
                            RTC_LOG(LS_INFO << "nChannels=" << Wfx.Format.nChannels 
                                            << ", nSamplesPerSec=" << Wfx.Format.nSamplesPerSec 
                                            << " is not supported. No closest match."
                            );
                        }
                    }
                }
                if (hr == S_OK)
                    break;
            }

            if (hr == S_OK) 
            {
                m_recAudioFrameSize = Wfx.Format.nBlockAlign;
                m_recSampleRate = Wfx.Format.nSamplesPerSec;
                m_recBlockSize = Wfx.Format.nSamplesPerSec / 100;
                m_recChannels = Wfx.Format.nChannels;

                RTC_LOG(LS_VERBOSE << "VoE selected this capturing format:");
                RTC_LOG(LS_VERBOSE << "wFormatTag        : 0x" << std::hex << Wfx.Format.wFormatTag << std::dec << " (" << Wfx.Format.wFormatTag << ")");
                RTC_LOG(LS_VERBOSE << "nChannels         : " << Wfx.Format.nChannels);
                RTC_LOG(LS_VERBOSE << "nSamplesPerSec    : " << Wfx.Format.nSamplesPerSec);
                RTC_LOG(LS_VERBOSE << "nAvgBytesPerSec   : " << Wfx.Format.nAvgBytesPerSec);
                RTC_LOG(LS_VERBOSE << "nBlockAlign       : " << Wfx.Format.nBlockAlign);
                RTC_LOG(LS_VERBOSE << "wBitsPerSample    : " << Wfx.Format.wBitsPerSample);
                RTC_LOG(LS_VERBOSE << "cbSize            : " << Wfx.Format.cbSize);
                RTC_LOG(LS_VERBOSE << "Additional settings:");
                RTC_LOG(LS_VERBOSE << "_recAudioFrameSize: " << m_recAudioFrameSize);
                RTC_LOG(LS_VERBOSE << "_recBlockSize     : " << m_recBlockSize);
                RTC_LOG(LS_VERBOSE << "_recChannels      : " << m_recChannels);
            }

            // Create a capturing stream.
            hr = m_ptrClientIn->Initialize(
                AUDCLNT_SHAREMODE_SHARED,  // share Audio Engine with other applications
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |  // processing of the audio buffer by
                                                     // the client will be event driven
                AUDCLNT_STREAMFLAGS_NOPERSIST,   // volume and mute settings for an
                                                 // audio session will not persist
                                                 // across system restarts
                0,                    // required for event-driven shared mode
                0,                    // periodicity
                (WAVEFORMATEX*)&Wfx,  // selected wave format
                NULL);                // session GUID

            if (hr != S_OK) 
                RTC_LOG(LS_ERROR << "IAudioClient::Initialize() failed:");
            
            if (FAILED(hr))
                break;

            if (m_ptrAudioBuffer) 
            {
                // Update the audio buffer with the selected parameters
                m_ptrAudioBuffer->SetRecordingSampleRate(m_recSampleRate);
                m_ptrAudioBuffer->SetRecordingChannels((uint8_t)m_recChannels);
            }
            else 
            {
                // We can enter this state during CoreAudioIsSupported() when no
                // AudioDeviceImplementation has been created, hence the AudioDeviceBuffer
                // does not exist. It is OK to end up here since we don't initiate any media
                // in CoreAudioIsSupported().
                RTC_LOG(LS_VERBOSE << "AudioDeviceBuffer must be attached before streaming can start");
            }

            // Get the actual size of the shared (endpoint buffer).
            // Typical value is 960 audio frames <=> 20ms @ 48kHz sample rate.
            UINT bufferFrameCount(0);
            if (SUCCEEDED(hr = m_ptrClientIn->GetBufferSize(&bufferFrameCount))) 
                RTC_LOG(LS_VERBOSE << "IAudioClient::GetBufferSize() => " << bufferFrameCount << " (<=> " << bufferFrameCount * m_recAudioFrameSize << " bytes)");

            // Set the event handle that the system signals when an audio buffer is ready
            // to be processed by the client.
            if (FAILED(hr = m_ptrClientIn->SetEventHandle(m_hCaptureSamplesReadyEvent)))
                break;

            // Get an IAudioCaptureClient interface.
            m_ptrCaptureClient.Release();
            if (FAILED(hr = m_ptrClientIn->GetService(__uuidof(IAudioCaptureClient), (void**)&m_ptrCaptureClient)))
                break;

            // Mark capture side as initialized
            m_recIsInitialized = true;

            CoTaskMemFree(pWfxIn);
            CoTaskMemFree(pWfxClosestMatch);

            RTC_LOG(LS_VERBOSE << "capture side is now initialized");
            return 0;
        }

        _TraceCOMError(hr);
        CoTaskMemFree(pWfxIn);
        CoTaskMemFree(pWfxClosestMatch);
        
        m_ptrClientIn.Release();
        m_ptrCaptureClient.Release();

        return -1;
    }

    // ----------------------------------------------------------------------------
    //  StartRecording
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StartRecording() {
        if (!m_recIsInitialized) {
            return -1;
        }

        if (m_hRecThread != NULL) {
            return 0;
        }

        if (m_recording) {
            return 0;
        }

        {
            rtc::CritScope critScoped(&m_critSect);

            // Create thread which will drive the capturing
            LPTHREAD_START_ROUTINE lpStartAddress = WSAPICaptureThread;
            if (m_builtInAecEnabled) 
            {
                // Redirect to the DMO polling method.
                lpStartAddress = WSAPICaptureThreadPollDMO;

                if (!m_playing) 
                {
                    // The DMO won't provide us captured output data unless we
                    // give it render data to process.
                    RTC_LOG(LS_ERROR << "Playout must be started before recording when using" << " the built-in AEC");
                    return -1;
                }
            }

            assert(m_hRecThread == NULL);
            m_hRecThread = CreateThread(NULL, 0, lpStartAddress, this, 0, NULL);
            if (m_hRecThread == NULL) 
            {
                RTC_LOG(LS_ERROR << "failed to create the recording thread");
                return -1;
            }

            // Set thread priority to highest possible
            SetThreadPriority(m_hRecThread, THREAD_PRIORITY_TIME_CRITICAL);

        }  // critScoped

        DWORD ret = WaitForSingleObject(m_hCaptureStartedEvent, 1000);
        if (ret != WAIT_OBJECT_0) 
        {
            RTC_LOG(LS_VERBOSE << "capturing did not start up properly");
            return -1;
        }

        RTC_LOG(LS_VERBOSE << "capture audio stream has now started...");

        m_recording = true;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  StopRecording
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StopRecording() 
    {
        int32_t err = 0;

        if (!m_recIsInitialized)
            return 0;

        _Lock();

        if (m_hRecThread == NULL) 
        {
            RTC_LOG(LS_VERBOSE << "no capturing stream is active => close down WASAPI only");
            m_ptrClientIn.Release();
            m_ptrCaptureClient.Release();
            m_recIsInitialized = false;
            m_recording = false;
            _UnLock();
            return 0;
        }

        // Stop the driving thread...
        RTC_LOG(LS_VERBOSE << "closing down the webrtc_core_audio_capture_thread...");
        // Manual-reset event; it will remain signalled to stop all capture threads.
        SetEvent(m_hShutdownCaptureEvent);

        _UnLock();

        DWORD ret = WaitForSingleObject(m_hRecThread, 2000);
        if (ret != WAIT_OBJECT_0) 
        {
            RTC_LOG(LS_ERROR << "failed to close down webrtc_core_audio_capture_thread");
            err = -1;
        }
        else 
            RTC_LOG(LS_VERBOSE << "webrtc_core_audio_capture_thread is now closed");

        _Lock();

        ResetEvent(m_hShutdownCaptureEvent);  // Must be manually reset.
                                             // Ensure that the thread has released these interfaces properly.
        assert(err == -1 || m_ptrClientIn == NULL);
        assert(err == -1 || m_ptrCaptureClient == NULL);

        m_recIsInitialized = false;
        m_recording = false;

        // These will create thread leaks in the result of an error,
        // but we can at least resume the call.
        CloseHandle(m_hRecThread);
        m_hRecThread = NULL;

        if (m_builtInAecEnabled) 
        {
            assert(m_dmo != NULL);
            // This is necessary. Otherwise the DMO can generate garbage render
            // audio even after rendering has stopped.
            HRESULT hr = m_dmo->FreeStreamingResources();
            if (FAILED(hr)) 
            {
                _TraceCOMError(hr);
                err = -1;
            }
        }

        // Reset the recording delay value.
        m_sndCardRecDelay = 0;

        _UnLock();

        return err;
    }

    // ----------------------------------------------------------------------------
    //  RecordingIsInitialized
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::RecordingIsInitialized() const 
    {
        return (m_recIsInitialized);
    }

    // ----------------------------------------------------------------------------
    //  Recording
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::Recording() const
    {
        return (m_recording);
    }

    // ----------------------------------------------------------------------------
    //  PlayoutIsInitialized
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::PlayoutIsInitialized() const
    {
        return (m_playIsInitialized);
    }

    // ----------------------------------------------------------------------------
    //  StartPlayout
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StartPlayout()
    {
        if (!m_playIsInitialized)
            return -1;

        if (m_hPlayThread != NULL)
            return 0;

        if (m_playing)
            return 0;

        {
            rtc::CritScope critScoped(&m_critSect);

            // Create thread which will drive the rendering.
            assert(m_hPlayThread == NULL);
            m_hPlayThread = CreateThread(NULL, 0, WSAPIRenderThread, this, 0, NULL);
            if (m_hPlayThread == NULL) 
            {
                RTC_LOG(LS_ERROR << "failed to create the playout thread");
                return -1;
            }

            // Set thread priority to highest possible.
            SetThreadPriority(m_hPlayThread, THREAD_PRIORITY_TIME_CRITICAL);
        }  // critScoped

        DWORD ret = WaitForSingleObject(m_hRenderStartedEvent, 1000);
        if (ret != WAIT_OBJECT_0) 
        {
            RTC_LOG(LS_VERBOSE << "rendering did not start up properly");
            return -1;
        }

        m_playing = true;
        RTC_LOG(LS_VERBOSE << "rendering audio stream has now started...");

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  StopPlayout
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::StopPlayout()
    {
        if (!m_playIsInitialized) 
            return 0;

        {
            rtc::CritScope critScoped(&m_critSect);

            if (m_hPlayThread == NULL)
            {
                RTC_LOG(LS_VERBOSE << "no rendering stream is active => close down WASAPI only");
                m_ptrClientOut.Release();
                m_ptrRenderClient.Release();
                m_playIsInitialized = false;
                m_playing = false;
                return 0;
            }

            // stop the driving thread...
            RTC_LOG(LS_VERBOSE << "closing down the webrtc_core_audio_render_thread...");
            SetEvent(m_hShutdownRenderEvent);
        }  // critScoped

        DWORD ret = WaitForSingleObject(m_hPlayThread, 2000);
        if (ret != WAIT_OBJECT_0)
        {
            // the thread did not stop as it should
            RTC_LOG(LS_ERROR << "failed to close down webrtc_core_audio_render_thread");
            CloseHandle(m_hPlayThread);
            m_hPlayThread = NULL;
            m_playIsInitialized = false;
            m_playing = false;
            return -1;
        }

        {
            rtc::CritScope critScoped(&m_critSect);
            RTC_LOG(LS_VERBOSE << "webrtc_core_audio_render_thread is now closed");

            // to reset this event manually at each time we finish with it,
            // in case that the render thread has exited before StopPlayout(),
            // this event might be caught by the new render thread within same VoE
            // instance.
            ResetEvent(m_hShutdownRenderEvent);

            m_ptrClientOut.Release();
            m_ptrRenderClient.Release();

            m_playIsInitialized = false;
            m_playing = false;

            CloseHandle(m_hPlayThread);
            m_hPlayThread = NULL;

            if (m_builtInAecEnabled && m_recording)
            {
                // The DMO won't provide us captured output data unless we
                // give it render data to process.
                //
                // We still permit the playout to shutdown, and trace a warning.
                // Otherwise, VoE can get into a state which will never permit
                // playout to stop properly.
                RTC_LOG(LS_WARNING << "Recording should be stopped before playout when using the" << " built-in AEC");
            }

            // Reset the playout delay value.
            m_sndCardPlayDelay = 0;
        }  // critScoped

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  PlayoutDelay
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::PlayoutDelay(uint16_t& delayMS) const {
        rtc::CritScope critScoped(&m_critSect);
        delayMS = static_cast<uint16_t>(m_sndCardPlayDelay);
        return 0;
    }

    // ----------------------------------------------------------------------------
    //  Playing
    // ----------------------------------------------------------------------------

    bool AudioDeviceWindowsCore::Playing() const {
        return (m_playing);
    }

    // ============================================================================
    //                                 Private Methods
    // ============================================================================

    // ----------------------------------------------------------------------------
    //  [static] WSAPIRenderThread
    // ----------------------------------------------------------------------------

    DWORD WINAPI AudioDeviceWindowsCore::WSAPIRenderThread(LPVOID context)
    {
        return reinterpret_cast<AudioDeviceWindowsCore*>(context)->DoRenderThread();
    }

    // ----------------------------------------------------------------------------
    //  [static] WSAPICaptureThread
    // ----------------------------------------------------------------------------

    DWORD WINAPI AudioDeviceWindowsCore::WSAPICaptureThread(LPVOID context)
    {
        return reinterpret_cast<AudioDeviceWindowsCore*>(context)->DoCaptureThread();
    }

    DWORD WINAPI AudioDeviceWindowsCore::WSAPICaptureThreadPollDMO(LPVOID context)
    {
        return reinterpret_cast<AudioDeviceWindowsCore*>(context)->DoCaptureThreadPollDMO();
    }

    // ----------------------------------------------------------------------------
    //  DoRenderThread
    // ----------------------------------------------------------------------------

    DWORD AudioDeviceWindowsCore::DoRenderThread()
    {
        IAudioClock* clock  = NULL;
        bool keepPlaying    = true;
        HANDLE hMmTask      = NULL;
        HRESULT hr          = S_OK;

        try
        {
            HANDLE waitArray[]{ m_hShutdownRenderEvent, m_hRenderSamplesReadyEvent };
            HRESULT err = S_OK;

            // Initialize COM as MTA in this thread.
            ScopedCOMInitializer comInit(ScopedCOMInitializer::kMTA);
            if (!comInit.succeeded()) {
                RTC_LOG(LS_ERROR << "failed to initialize COM in render thread");
                throw S_FALSE;
            }

            rtc::SetCurrentThreadName("webrtc_core_audio_render_thread");

            // Use Multimedia Class Scheduler Service (MMCSS) to boost the thread
            // priority.
            //
            if (_winSupportAvrt)
            {
                DWORD taskIndex(0);
                hMmTask = _PAvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
                if (hMmTask)
                {
                    if (FALSE == _PAvSetMmThreadPriority(hMmTask, AVRT_PRIORITY_CRITICAL))
                        RTC_LOG(LS_WARNING << "failed to boost play-thread using MMCSS");

                    RTC_LOG(LS_VERBOSE << "render thread is now registered with MMCSS (taskIndex=" << taskIndex << ")");
                }
                else
                {
                    RTC_LOG(LS_WARNING << "failed to enable MMCSS on render thread (err=" << GetLastError() << ")");
                    _TraceCOMError(GetLastError());
                }
            }

            _Lock();

            // Get size of rendering buffer (length is expressed as the number of audio
            // frames the buffer can hold). This value is fixed during the rendering
            // session.
            //
            UINT32 bufferLength = 0;
            err = m_ptrClientOut->GetBufferSize(&bufferLength);
            if (FAILED(err))
                throw err;

            RTC_LOG(LS_VERBOSE << "[REND] size of buffer       : " << bufferLength );

            // Get maximum latency for the current stream (will not change for the
            // lifetime  of the IAudioClient object).
            //
            REFERENCE_TIME latency;
            m_ptrClientOut->GetStreamLatency(&latency);
            RTC_LOG(LS_VERBOSE << "[REND] max stream latency   : " << (DWORD)latency << " (" << (double)(latency / 10000.0) << " ms)");

            // Get the length of the periodic interval separating successive processing
            // passes by the audio engine on the data in the endpoint buffer.
            //
            // The period between processing passes by the audio engine is fixed for a
            // particular audio endpoint device and represents the smallest processing
            // quantum for the audio engine. This period plus the stream latency between
            // the buffer and endpoint device represents the minimum possible latency that
            // an audio application can achieve. Typical value: 100000 <=> 0.01 sec =
            // 10ms.
            //
            REFERENCE_TIME devPeriod = 0;
            REFERENCE_TIME devPeriodMin = 0;
            m_ptrClientOut->GetDevicePeriod(&devPeriod, &devPeriodMin);
            RTC_LOG(LS_VERBOSE << "[REND] device period        : " << (DWORD)devPeriod << " (" << (double)(devPeriod / 10000.0) << " ms)");

            // Derive initial rendering delay.
            // Example: 10*(960/480) + 15 = 20 + 15 = 35ms
            //
            int playout_delay = 10 * (bufferLength / m_playBlockSize) + (int)((latency + devPeriod) / 10000);
            m_sndCardPlayDelay = playout_delay;
            m_writtenSamples = 0;
            RTC_LOG(LS_VERBOSE << "[REND] initial delay        : " << playout_delay);

            double endpointBufferSizeMS = 10.0 * ((double)bufferLength / (double)m_devicePlayBlockSize);
            RTC_LOG(LS_VERBOSE << "[REND] endpointBufferSizeMS : " << endpointBufferSizeMS);

            // Before starting the stream, fill the rendering buffer with silence.
            //
            BYTE* pData = NULL;
            err = m_ptrRenderClient->GetBuffer(bufferLength, &pData);
            if(FAILED(err))
                throw err;

            err = m_ptrRenderClient->ReleaseBuffer(bufferLength, AUDCLNT_BUFFERFLAGS_SILENT);
            if (FAILED(err))
                throw err;

            m_writtenSamples += bufferLength;

            err = m_ptrClientOut->GetService(__uuidof(IAudioClock), (void**)&clock);
            if (FAILED(err))
                RTC_LOG(LS_WARNING << "failed to get IAudioClock interface from the IAudioClient");

            // Start up the rendering audio stream.
            err = m_ptrClientOut->Start();
            if (FAILED(err))
                throw err;

            _UnLock();

            // Set event which will ensure that the calling thread modifies the playing
            // state to true.
            //
            SetEvent(m_hRenderStartedEvent);

            // >> ------------------ THREAD LOOP ------------------

            while (keepPlaying)
            {
                // Wait for a render notification event or a shutdown event
                DWORD waitResult = WaitForMultipleObjects(2, waitArray, FALSE, 500);
                switch (waitResult)
                {
                case WAIT_OBJECT_0 + 0:  // _hShutdownRenderEvent
                    keepPlaying = false;
                    break;
                case WAIT_OBJECT_0 + 1:  // _hRenderSamplesReadyEvent
                    break;
                case WAIT_TIMEOUT:  // timeout notification
                    RTC_LOG(LS_WARNING << "render event timed out after 0.5 seconds");
                    throw err;
                default:  // unexpected error
                    RTC_LOG(LS_WARNING << "unknown wait termination on render side");
                    throw err;
                }

                while (keepPlaying)
                {
                    _Lock();

                    // Sanity check to ensure that essential states are not modified
                    // during the unlocked period.
                    if (m_ptrRenderClient == NULL || m_ptrClientOut == NULL)
                    {
                        _UnLock();
                        RTC_LOG(LS_ERROR << "output state has been modified during unlocked period");
                        throw err;
                    }

                    // Get the number of frames of padding (queued up to play) in the endpoint
                    // buffer.
                    UINT32 padding = 0;
                    err = m_ptrClientOut->GetCurrentPadding(&padding);
                    if (FAILED(err))
                        throw err;

                    // Derive the amount of available space in the output buffer
                    uint32_t framesAvailable = bufferLength - padding;

                    // Do we have 10 ms available in the render buffer?
                    if (framesAvailable < m_playBlockSize)
                    {
                        // Not enough space in render buffer to store next render packet.
                        _UnLock();
                        break;
                    }

                    // Write n*10ms buffers to the render buffer
                    const uint32_t n10msBuffers = (framesAvailable / m_playBlockSize);
                    for (uint32_t n = 0; n < n10msBuffers; n++)
                    {
                        // Get pointer (i.e., grab the buffer) to next space in the shared
                        // render buffer.
                        err = m_ptrRenderClient->GetBuffer(m_playBlockSize, &pData);
                        if (FAILED(err))
                            throw err;

                        if (m_ptrAudioBuffer)
                        {
                            // Request data to be played out (#bytes =
                            // _playBlockSize*_audioFrameSize)
                            _UnLock();
                            int32_t nSamples = m_ptrAudioBuffer->RequestPlayoutData(m_playBlockSize);
                            _Lock();

                            if (nSamples == -1) 
                            {
                                _UnLock();
                                RTC_LOG(LS_ERROR << "failed to read data from render client");
                                throw err;
                            }

                            // Sanity check to ensure that essential states are not modified
                            // during the unlocked period
                            if (m_ptrRenderClient == NULL || m_ptrClientOut == NULL)
                            {
                                _UnLock();
                                RTC_LOG(LS_ERROR << "output state has been modified during unlocked" << " period");
                                throw err;
                            }

                            if (nSamples != static_cast<int32_t>(m_playBlockSize))
                                RTC_LOG(LS_WARNING << "nSamples(" << nSamples << ") != _playBlockSize" << m_playBlockSize << ")");

                            // Get the actual (stored) data
                            nSamples = m_ptrAudioBuffer->GetPlayoutData((int8_t*)pData);
                        }

                        DWORD dwFlags(0);
                        err = m_ptrRenderClient->ReleaseBuffer(m_playBlockSize, dwFlags);
                        // See http://msdn.microsoft.com/en-us/library/dd316605(VS.85).aspx
                        // for more details regarding AUDCLNT_E_DEVICE_INVALIDATED.
                        if (FAILED(err))
                            throw err;

                        m_writtenSamples += m_playBlockSize;
                    }

                    // Check the current delay on the playout side.
                    if (clock)
                    {
                        UINT64 pos = 0;
                        UINT64 freq = 1;
                        clock->GetPosition(&pos, NULL);
                        clock->GetFrequency(&freq);
                        playout_delay = ROUND((double(m_writtenSamples) / m_devicePlaySampleRate - double(pos) / freq) * 1000.0);
                        m_sndCardPlayDelay = playout_delay;
                    }

                    _UnLock();
                }
            }

            // ------------------ THREAD LOOP ------------------ <<

            ::Sleep(static_cast<DWORD>(endpointBufferSizeMS + 0.5));
            throw m_ptrClientOut->Stop();
        }
        catch(HRESULT e)
        {
            SAFE_RELEASE(clock);

            if (FAILED(e))
            {
                m_ptrClientOut->Stop();
                _UnLock();
                _TraceCOMError(e);
            }

            if (_winSupportAvrt)
            {
                if (NULL != hMmTask)
                    _PAvRevertMmThreadCharacteristics(hMmTask);
            }

            _Lock();

            if (keepPlaying)
            {
                if (m_ptrClientOut != NULL)
                {
                    e = m_ptrClientOut->Stop();
                    if (FAILED(e)) _TraceCOMError(e);

                    e = m_ptrClientOut->Reset();

                    if (FAILED(e)) _TraceCOMError(e);
                }
                RTC_LOG(LS_ERROR << "Playout error: rendering thread has ended pre-maturely");
            }
            else
                RTC_LOG(LS_VERBOSE << "_Rendering thread is now terminated properly");

            _UnLock();

            hr = e;
        }

        return (DWORD)hr;
    }

    DWORD AudioDeviceWindowsCore::InitCaptureThreadPriority() {
        m_hMmTask = NULL;

        rtc::SetCurrentThreadName("webrtc_core_audio_capture_thread");

        // Use Multimedia Class Scheduler Service (MMCSS) to boost the thread
        // priority.
        if (_winSupportAvrt) 
        {
            DWORD taskIndex(0);
            m_hMmTask = _PAvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
            if (m_hMmTask) 
            {
                if (!_PAvSetMmThreadPriority(m_hMmTask, AVRT_PRIORITY_CRITICAL)) 
                    RTC_LOG(LS_WARNING << "failed to boost rec-thread using MMCSS");

                RTC_LOG(LS_VERBOSE << "capture thread is now registered with MMCSS (taskIndex=" << taskIndex << ")");
            }
            else 
            {
                RTC_LOG(LS_WARNING << "failed to enable MMCSS on capture thread (err=" << GetLastError() << ")");
                _TraceCOMError(GetLastError());
            }
        }

        return S_OK;
    }

    void AudioDeviceWindowsCore::RevertCaptureThreadPriority() 
    {
        if (_winSupportAvrt) 
        {
            if (NULL != m_hMmTask) 
                _PAvRevertMmThreadCharacteristics(m_hMmTask);
        }

        m_hMmTask = NULL;
    }

    DWORD AudioDeviceWindowsCore::DoCaptureThreadPollDMO() 
    {
        assert(m_mediaBuffer != NULL);
        bool keepRecording = true;

        // Initialize COM as MTA in this thread.
        ScopedCOMInitializer comInit(ScopedCOMInitializer::kMTA);
        if (!comInit.succeeded()) 
        {
            RTC_LOG(LS_ERROR << "failed to initialize COM in polling DMO thread");
            return 1;
        }

        HRESULT hr = InitCaptureThreadPriority();
        if (FAILED(hr))
            return hr;

        // Set event which will ensure that the calling thread modifies the
        // recording state to true.
        SetEvent(m_hCaptureStartedEvent);

        // >> ---------------------------- THREAD LOOP ----------------------------
        while (keepRecording) 
        {
            // Poll the DMO every 5 ms.
            // (The same interval used in the Wave implementation.)
            DWORD waitResult = WaitForSingleObject(m_hShutdownCaptureEvent, 5);
            switch (waitResult) 
            {
            case WAIT_OBJECT_0:  // _hShutdownCaptureEvent
                keepRecording = false;
                break;
            case WAIT_TIMEOUT:  // timeout notification
                break;
            default:  // unexpected error
                RTC_LOG(LS_WARNING << "Unknown wait termination on capture side");
                hr = -1;  // To signal an error callback.
                keepRecording = false;
                break;
            }

            while (keepRecording) 
            {
                rtc::CritScope critScoped(&m_critSect);

                DWORD dwStatus = 0;
                {
                    DMO_OUTPUT_DATA_BUFFER dmoBuffer = { 0 };
                    dmoBuffer.pBuffer = m_mediaBuffer;
                    dmoBuffer.pBuffer->AddRef();

                    // Poll the DMO for AEC processed capture data. The DMO will
                    // copy available data to |dmoBuffer|, and should only return
                    // 10 ms frames. The value of |dwStatus| should be ignored.
                    hr = m_dmo->ProcessOutput(0, 1, &dmoBuffer, &dwStatus);
                    SAFE_RELEASE(dmoBuffer.pBuffer);
                    dwStatus = dmoBuffer.dwStatus;
                }
                
                if (FAILED(hr)) 
                {
                    _TraceCOMError(hr);
                    keepRecording = false;
                    assert(false);
                    break;
                }

                ULONG bytesProduced = 0;
                BYTE* data;
                // Get a pointer to the data buffer. This should be valid until
                // the next call to ProcessOutput.
                hr = m_mediaBuffer->GetBufferAndLength(&data, &bytesProduced);
                if (FAILED(hr)) 
                {
                    _TraceCOMError(hr);
                    keepRecording = false;
                    assert(false);
                    break;
                }

                if (bytesProduced > 0) 
                {
                    const int kSamplesProduced = bytesProduced / m_recAudioFrameSize;
                    // TODO(andrew): verify that this is always satisfied. It might
                    // be that ProcessOutput will try to return more than 10 ms if
                    // we fail to call it frequently enough.
                    assert(kSamplesProduced == static_cast<int>(m_recBlockSize));
                    assert(sizeof(BYTE) == sizeof(int8_t));
                    m_ptrAudioBuffer->SetRecordedBuffer(reinterpret_cast<int8_t*>(data),
                        kSamplesProduced);
                    m_ptrAudioBuffer->SetVQEData(0, 0);

                    _UnLock();  // Release lock while making the callback.
                    m_ptrAudioBuffer->DeliverRecordedData();
                    _Lock();
                }

                // Reset length to indicate buffer availability.
                hr = m_mediaBuffer->SetLength(0);
                if (FAILED(hr)) 
                {
                    _TraceCOMError(hr);
                    keepRecording = false;
                    assert(false);
                    break;
                }

                if (!(dwStatus & DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE)) 
                {
                    // The DMO cannot currently produce more data. This is the
                    // normal case; otherwise it means the DMO had more than 10 ms
                    // of data available and ProcessOutput should be called again.
                    break;
                }
            }
        }
        // ---------------------------- THREAD LOOP ---------------------------- <<

        RevertCaptureThreadPriority();

        if (FAILED(hr))
        {
            RTC_LOG(LS_ERROR << "Recording error: capturing thread has ended prematurely");
        }
        else
        {
            RTC_LOG(LS_VERBOSE << "Capturing thread is now terminated properly");
        }

        return hr;
    }

    // ----------------------------------------------------------------------------
    //  DoCaptureThread
    // ----------------------------------------------------------------------------

    DWORD AudioDeviceWindowsCore::DoCaptureThread() 
    {
        HRESULT hr = S_OK;
        bool keepRecording = true;

        try
        {
            HRESULT e = S_OK;

            HANDLE waitArray[2] = { m_hShutdownCaptureEvent, m_hCaptureSamplesReadyEvent };

            LARGE_INTEGER t1;

            std::unique_ptr<BYTE> syncBuffer;
            UINT32 syncBufIndex = 0;

            m_readSamples = 0;

            // Initialize COM as MTA in this thread.
            ScopedCOMInitializer comInit(ScopedCOMInitializer::kMTA);
            if (!comInit.succeeded()) 
            {
                RTC_LOG(LS_ERROR << "failed to initialize COM in capture thread");
                throw S_FALSE;
            }

            e = InitCaptureThreadPriority();
            if (FAILED(e))
                throw e;

            _Lock();

            // Get size of capturing buffer (length is expressed as the number of audio
            // frames the buffer can hold). This value is fixed during the capturing
            // session.
            //
            UINT32 bufferLength = 0;
            if (m_ptrClientIn == NULL) 
            {
                RTC_LOG(LS_ERROR << "input state has been modified before capture loop starts.");
                throw e;
            }

            e = m_ptrClientIn->GetBufferSize(&bufferLength);
            if (FAILED(e))
                throw e;

            RTC_LOG(LS_VERBOSE << "[CAPT] size of buffer       : " << bufferLength);

            // Allocate memory for sync buffer.
            // It is used for compensation between native 44.1 and internal 44.0 and
            // for cases when the capture buffer is larger than 10ms.
            //
            const UINT32 syncBufferSize = 2 * (bufferLength * m_recAudioFrameSize);
            syncBuffer.reset(new BYTE[syncBufferSize]);
            if (!syncBuffer)
                throw E_POINTER;

            RTC_LOG(LS_VERBOSE << "[CAPT] size of sync buffer  : " << syncBufferSize << " [bytes]");

            // Get maximum latency for the current stream (will not change for the
            // lifetime of the IAudioClient object).
            //
            REFERENCE_TIME latency;
            m_ptrClientIn->GetStreamLatency(&latency);
            RTC_LOG(LS_VERBOSE << "[CAPT] max stream latency   : " << (DWORD)latency << " (" << (double)(latency / 10000.0) << " ms)");

            // Get the length of the periodic interval separating successive processing
            // passes by the audio engine on the data in the endpoint buffer.
            //
            REFERENCE_TIME devPeriod = 0;
            REFERENCE_TIME devPeriodMin = 0;
            m_ptrClientIn->GetDevicePeriod(&devPeriod, &devPeriodMin);
            RTC_LOG(LS_VERBOSE << "[CAPT] device period        : " << (DWORD)devPeriod << " (" << (double)(devPeriod / 10000.0) << " ms)");

            double extraDelayMS = (double)((latency + devPeriod) / 10000.0);
            RTC_LOG(LS_VERBOSE << "[CAPT] extraDelayMS         : " << extraDelayMS);

            double endpointBufferSizeMS = 10.0 * ((double)bufferLength / (double)m_recBlockSize);
            RTC_LOG(LS_VERBOSE << "[CAPT] endpointBufferSizeMS : " << endpointBufferSizeMS);

            // Start up the capturing stream.
            //
            e = m_ptrClientIn->Start();
            if(FAILED(e))
                throw e;

            _UnLock();

            // Set event which will ensure that the calling thread modifies the recording
            // state to true.
            //
            SetEvent(m_hCaptureStartedEvent);

            // >> ---------------------------- THREAD LOOP ----------------------------

            while (keepRecording) 
            {
                // Wait for a capture notification event or a shutdown event
                switch (WaitForMultipleObjects(2, waitArray, FALSE, 500))
                {
                case WAIT_OBJECT_0 + 0:  // _hShutdownCaptureEvent
                    keepRecording = false;
                    break;
                case WAIT_OBJECT_0 + 1:  // _hCaptureSamplesReadyEvent
                    break;
                case WAIT_TIMEOUT:  // timeout notification
                    RTC_LOG(LS_WARNING << "capture event timed out after 0.5 seconds");
                    throw e;
                default:  // unexpected error
                    RTC_LOG(LS_WARNING << "unknown wait termination on capture side");
                    throw e;
                }

                while (keepRecording)
                {
                    BYTE* pData = 0;
                    UINT32 framesAvailable = 0;
                    DWORD flags = 0;
                    UINT64 recTime = 0;
                    UINT64 recPos = 0;

                    _Lock();

                    // Sanity check to ensure that essential states are not modified
                    // during the unlocked period.
                    if (m_ptrCaptureClient == NULL || m_ptrClientIn == NULL)
                    {
                        _UnLock();
                        RTC_LOG(LS_ERROR << "input state has been modified during unlocked period");
                        throw e;
                    }

                    //  Find out how much capture data is available
                    //
                    e = m_ptrCaptureClient->GetBuffer(
                        &pData,            // packet which is ready to be read by used
                        &framesAvailable,  // #frames in the captured packet (can be zero)
                        &flags,            // support flags (check)
                        &recPos,    // device position of first audio frame in data packet
                        &recTime);  // value of performance counter at the time of recording
                                    // the first audio frame

                    if (SUCCEEDED(e))
                    {
                        if (AUDCLNT_S_BUFFER_EMPTY == e)
                        {
                            // Buffer was empty => start waiting for a new capture notification
                            // event
                            _UnLock();
                            break;
                        }

                        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                        {
                            // Treat all of the data in the packet as silence and ignore the
                            // actual data values.
                            RTC_LOG(LS_WARNING << "AUDCLNT_BUFFERFLAGS_SILENT");
                            pData = nullptr;
                        }

                        assert(framesAvailable != 0);

                        if (pData)
                            CopyMemory(&(syncBuffer.get())[syncBufIndex * m_recAudioFrameSize], pData, framesAvailable * m_recAudioFrameSize);
                        else
                            ZeroMemory(&(syncBuffer.get())[syncBufIndex * m_recAudioFrameSize], framesAvailable * m_recAudioFrameSize);

                        assert(syncBufferSize >= (syncBufIndex * m_recAudioFrameSize) + framesAvailable * m_recAudioFrameSize);

                        // Release the capture buffer
                        //
                        e = m_ptrCaptureClient->ReleaseBuffer(framesAvailable);
                        if(FAILED(e))
                            throw e;

                        m_readSamples += framesAvailable;
                        syncBufIndex += framesAvailable;

                        QueryPerformanceCounter(&t1);

                        // Get the current recording and playout delay.
                        uint32_t sndCardRecDelay = (uint32_t)(((((UINT64)t1.QuadPart * m_perfCounterFactor) - recTime) / 10000) + (10 * syncBufIndex) / m_recBlockSize - 10);
                        uint32_t sndCardPlayDelay = static_cast<uint32_t>(m_sndCardPlayDelay);

                        m_sndCardRecDelay = sndCardRecDelay;

                        while (syncBufIndex >= m_recBlockSize)
                        {
                            if (m_ptrAudioBuffer)
                            {
                                m_ptrAudioBuffer->SetRecordedBuffer(syncBuffer.get(), m_recBlockSize);
                                m_ptrAudioBuffer->SetVQEData(sndCardPlayDelay, sndCardRecDelay);
                                m_ptrAudioBuffer->SetTypingStatus(KeyPressed());

                                _UnLock();  // release lock while making the callback
                                m_ptrAudioBuffer->DeliverRecordedData();
                                _Lock();  // restore the lock

                                          // Sanity check to ensure that essential states are not modified
                                          // during the unlocked period
                                if (m_ptrCaptureClient == nullptr || m_ptrClientIn == nullptr)
                                {
                                    _UnLock();
                                    RTC_LOG(LS_ERROR << "input state has been modified during unlocked period");
                                    throw e;
                                }
                            }

                            // store remaining data which was not able to deliver as 10ms segment
                            MoveMemory(&(syncBuffer.get())[0], &(syncBuffer.get())[m_recBlockSize * m_recAudioFrameSize], (syncBufIndex - m_recBlockSize) * m_recAudioFrameSize);
                            syncBufIndex -= m_recBlockSize;
                            sndCardRecDelay -= 10;
                        }
                    }
                    else {
                        // If GetBuffer returns AUDCLNT_E_BUFFER_ERROR, the thread consuming the
                        // audio samples must wait for the next processing pass. The client
                        // might benefit from keeping a count of the failed GetBuffer calls. If
                        // GetBuffer returns this error repeatedly, the client can start a new
                        // processing loop after shutting down the current client by calling
                        // IAudioClient::Stop, IAudioClient::Reset, and releasing the audio
                        // client.
                        RTC_LOG(LS_ERROR << "IAudioCaptureClient::GetBuffer returned AUDCLNT_E_BUFFER_ERROR, hr = 0x" << std::hex << e << std::dec);
                        throw e;;
                    }

                    _UnLock();
                }
            }

            // ---------------------------- THREAD LOOP ---------------------------- <<

            if (m_ptrClientIn)
                e = m_ptrClientIn->Stop();

            throw e;
        }
        catch(HRESULT e)
        {
            if (FAILED(e))
            {
                m_ptrClientIn->Stop();
                _UnLock();
                _TraceCOMError(e);
            }

            RevertCaptureThreadPriority();

            _Lock();

            if (keepRecording)
            {
                if (m_ptrClientIn != nullptr)
                {
                    e = m_ptrClientIn->Stop();
                    if (FAILED(e))
                        _TraceCOMError(e);

                    e = m_ptrClientIn->Reset();
                    if (FAILED(e))
                        _TraceCOMError(e);
                }

                RTC_LOG(LS_ERROR << "Recording error: capturing thread has ended pre-maturely");
            }
            else 
                RTC_LOG(LS_VERBOSE << "_Capturing thread is now terminated properly");

            m_ptrClientIn.Release();
            m_ptrCaptureClient.Release();

            _UnLock();

            hr = e;
        }

        return (DWORD)hr;
    }

    int32_t AudioDeviceWindowsCore::EnableBuiltInAEC(bool enable) 
    {
        if (m_recIsInitialized) 
        {
            RTC_LOG(LS_ERROR << "Attempt to set Windows AEC with recording already initialized");
            return -1;
        }

        if (m_dmo == NULL) 
        {
            RTC_LOG(LS_ERROR << "Built-in AEC DMO was not initialized properly at create time");
            return -1;
        }

        m_builtInAecEnabled = enable;
        return 0;
    }

    int AudioDeviceWindowsCore::SetDMOProperties() {
        HRESULT hr = S_OK;
        assert(m_dmo != NULL);

        CComPtr<IPropertyStore> ps;

        {
            CComPtr<IPropertyStore> ptrPS = NULL;
            hr = m_dmo->QueryInterface(IID_IPropertyStore, reinterpret_cast<void**>(&ptrPS));
            if (FAILED(hr) || ptrPS == NULL) 
            {
                _TraceCOMError(hr);
                return -1;
            }
            ps = ptrPS;
        }

        // Set the AEC system mode.
        // SINGLE_CHANNEL_AEC - AEC processing only.
        if (SetVtI4Property(ps, MFPKEY_WMAAECMA_SYSTEM_MODE, SINGLE_CHANNEL_AEC))
            return -1;

        // Set the AEC source mode.
        // VARIANT_TRUE - Source mode (we poll the AEC for captured data).
        if (SetBoolProperty(ps, MFPKEY_WMAAECMA_DMO_SOURCE_MODE, VARIANT_TRUE) == -1)
            return -1;

        // Enable the feature mode.
        // This lets us override all the default processing settings below.
        if (SetBoolProperty(ps, MFPKEY_WMAAECMA_FEATURE_MODE, VARIANT_TRUE) == -1)
            return -1;

        // Disable analog AGC (default enabled).
        if (SetBoolProperty(ps, MFPKEY_WMAAECMA_MIC_GAIN_BOUNDER, VARIANT_FALSE) == -1)
            return -1;

        // Disable noise suppression (default enabled).
        // 0 - Disabled, 1 - Enabled
        if (SetVtI4Property(ps, MFPKEY_WMAAECMA_FEATR_NS, 0) == -1)
            return -1;

        // Relevant parameters to leave at default settings:
        // MFPKEY_WMAAECMA_FEATR_AGC - Digital AGC (disabled).
        // MFPKEY_WMAAECMA_FEATR_CENTER_CLIP - AEC center clipping (enabled).
        // MFPKEY_WMAAECMA_FEATR_ECHO_LENGTH - Filter length (256 ms).
        //   TODO(andrew): investigate decresing the length to 128 ms.
        // MFPKEY_WMAAECMA_FEATR_FRAME_SIZE - Frame size (0).
        //   0 is automatic; defaults to 160 samples (or 10 ms frames at the
        //   selected 16 kHz) as long as mic array processing is disabled.
        // MFPKEY_WMAAECMA_FEATR_NOISE_FILL - Comfort noise (enabled).
        // MFPKEY_WMAAECMA_FEATR_VAD - VAD (disabled).

        // Set the devices selected by VoE. If using a default device, we need to
        // search for the device index.
        int inDevIndex = m_inputDeviceIndex;
        int outDevIndex = m_outputDeviceIndex;
        if (!m_usingInputDeviceIndex) 
        {
            ERole role = eCommunications;
            if (m_inputDevice == AudioDeviceModule::kDefaultDevice) 
                role = eConsole;

            if (_GetDefaultDeviceIndex(eCapture, role, &inDevIndex) == -1) 
                return -1;
        }

        if (!m_usingOutputDeviceIndex) 
        {
            ERole role = eCommunications;
            if (m_outputDevice == AudioDeviceModule::kDefaultDevice) 
                role = eConsole;

            if (_GetDefaultDeviceIndex(eRender, role, &outDevIndex) == -1) 
                return -1;
        }

        DWORD devIndex = static_cast<uint32_t>(outDevIndex << 16) + static_cast<uint32_t>(0x0000ffff & inDevIndex);
        RTC_LOG(LS_VERBOSE << "Capture device index: " << inDevIndex << ", render device index: " << outDevIndex);
        if (SetVtI4Property(ps, MFPKEY_WMAAECMA_DEVICE_INDEXES, devIndex) == -1)
            return -1;

        return 0;
    }

    int AudioDeviceWindowsCore::SetBoolProperty(IPropertyStore* ptrPS, REFPROPERTYKEY key, VARIANT_BOOL value) 
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_BOOL;
        pv.boolVal = value;
        HRESULT hr = ptrPS->SetValue(key, pv);
        PropVariantClear(&pv);

        if (SUCCEEDED(hr))
            return 0;

        _TraceCOMError(hr);
        return -1;
    }

    int AudioDeviceWindowsCore::SetVtI4Property(IPropertyStore* ptrPS, REFPROPERTYKEY key, LONG value) 
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I4;
        pv.lVal = value;
        HRESULT hr = ptrPS->SetValue(key, pv);
        PropVariantClear(&pv);

        if (SUCCEEDED(hr))
            return 0;

        _TraceCOMError(hr);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  _RefreshDeviceList
    //
    //  Creates a new list of endpoint rendering or capture devices after
    //  deleting any previously created (and possibly out-of-date) list of
    //  such devices.
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_RefreshDeviceList(EDataFlow dir) 
    {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        CComPtr<IMMDeviceCollection> pCollection;

        assert(dir == eRender || dir == eCapture);
        assert(m_ptrEnumerator != NULL);

        // Create a fresh list of devices using the specified direction
        hr = m_ptrEnumerator->EnumAudioEndpoints(dir, DEVICE_STATE_ACTIVE, &pCollection);
        if (FAILED(hr)) 
        {
            _TraceCOMError(hr);
            return -1;
        }

        if (dir == eRender)
            m_ptrRenderCollection = pCollection;
        else
            m_ptrCaptureCollection = pCollection;

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  _DeviceListCount
    //
    //  Gets a count of the endpoint rendering or capture devices in the
    //  current list of such devices.
    // ----------------------------------------------------------------------------

    int16_t AudioDeviceWindowsCore::_DeviceListCount(EDataFlow dir) 
    {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        UINT count = 0;

        assert(eRender == dir || eCapture == dir);

        if (eRender == dir && NULL != m_ptrRenderCollection) 
            hr = m_ptrRenderCollection->GetCount(&count);
        else if (NULL != m_ptrCaptureCollection)
            hr = m_ptrCaptureCollection->GetCount(&count);

        if(SUCCEEDED(hr))
            return static_cast<int16_t>(count);

        _TraceCOMError(hr);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  _GetListDeviceName
    //
    //  Gets the friendly name of an endpoint rendering or capture device
    //  from the current list of such devices. The caller uses an index
    //  into the list to identify the device.
    //
    //  Uses: _ptrRenderCollection or _ptrCaptureCollection which is updated
    //  in _RefreshDeviceList().
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetListDeviceName(EDataFlow dir, int index, LPWSTR szBuffer, int bufferLen) 
    {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        IMMDevice* pDevice = NULL;

        assert(dir == eRender || dir == eCapture);

        if (eRender == dir && NULL != m_ptrRenderCollection)
            hr = m_ptrRenderCollection->Item(index, &pDevice);
        else if (NULL != m_ptrCaptureCollection)
            hr = m_ptrCaptureCollection->Item(index, &pDevice);

        if (FAILED(hr)) 
        {
            _TraceCOMError(hr);
            SAFE_RELEASE(pDevice);
            return -1;
        }

        int32_t res = _GetDeviceName(pDevice, szBuffer, bufferLen);
        SAFE_RELEASE(pDevice);
        return res;
    }

    // ----------------------------------------------------------------------------
    //  _GetDefaultDeviceName
    //
    //  Gets the friendly name of an endpoint rendering or capture device
    //  given a specified device role.
    //
    //  Uses: _ptrEnumerator
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetDefaultDeviceName(EDataFlow dir,
        ERole role,
        LPWSTR szBuffer,
        int bufferLen) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        IMMDevice* pDevice = NULL;

        assert(dir == eRender || dir == eCapture);
        assert(role == eConsole || role == eCommunications);
        assert(m_ptrEnumerator != NULL);

        hr = m_ptrEnumerator->GetDefaultAudioEndpoint(dir, role, &pDevice);

        if (FAILED(hr)) {
            _TraceCOMError(hr);
            SAFE_RELEASE(pDevice);
            return -1;
        }

        int32_t res = _GetDeviceName(pDevice, szBuffer, bufferLen);
        SAFE_RELEASE(pDevice);
        return res;
    }

    // ----------------------------------------------------------------------------
    //  _GetListDeviceID
    //
    //  Gets the unique ID string of an endpoint rendering or capture device
    //  from the current list of such devices. The caller uses an index
    //  into the list to identify the device.
    //
    //  Uses: _ptrRenderCollection or _ptrCaptureCollection which is updated
    //  in _RefreshDeviceList().
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetListDeviceID(EDataFlow dir,
        int index,
        LPWSTR szBuffer,
        int bufferLen) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        IMMDevice* pDevice = NULL;

        assert(dir == eRender || dir == eCapture);

        if (eRender == dir && NULL != m_ptrRenderCollection) {
            hr = m_ptrRenderCollection->Item(index, &pDevice);
        }
        else if (NULL != m_ptrCaptureCollection) {
            hr = m_ptrCaptureCollection->Item(index, &pDevice);
        }

        if (FAILED(hr)) {
            _TraceCOMError(hr);
            SAFE_RELEASE(pDevice);
            return -1;
        }

        int32_t res = _GetDeviceID(pDevice, szBuffer, bufferLen);
        SAFE_RELEASE(pDevice);
        return res;
    }

    // ----------------------------------------------------------------------------
    //  _GetDefaultDeviceID
    //
    //  Gets the uniqe device ID of an endpoint rendering or capture device
    //  given a specified device role.
    //
    //  Uses: _ptrEnumerator
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetDefaultDeviceID(EDataFlow dir,
        ERole role,
        LPWSTR szBuffer,
        int bufferLen) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        IMMDevice* pDevice = NULL;

        assert(dir == eRender || dir == eCapture);
        assert(role == eConsole || role == eCommunications);
        assert(m_ptrEnumerator != NULL);

        hr = m_ptrEnumerator->GetDefaultAudioEndpoint(dir, role, &pDevice);

        if (FAILED(hr)) {
            _TraceCOMError(hr);
            SAFE_RELEASE(pDevice);
            return -1;
        }

        int32_t res = _GetDeviceID(pDevice, szBuffer, bufferLen);
        SAFE_RELEASE(pDevice);
        return res;
    }

    int32_t AudioDeviceWindowsCore::_GetDefaultDeviceIndex(EDataFlow dir,
        ERole role,
        int* index) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr = S_OK;
        WCHAR szDefaultDeviceID[MAX_PATH] = { 0 };
        WCHAR szDeviceID[MAX_PATH] = { 0 };

        const size_t kDeviceIDLength = sizeof(szDeviceID) / sizeof(szDeviceID[0]);
        assert(kDeviceIDLength ==
            sizeof(szDefaultDeviceID) / sizeof(szDefaultDeviceID[0]));

        if (_GetDefaultDeviceID(dir, role, szDefaultDeviceID, kDeviceIDLength) ==
            -1) {
            return -1;
        }

        IMMDeviceCollection* collection = m_ptrCaptureCollection;
        if (dir == eRender) {
            collection = m_ptrRenderCollection;
        }

        if (!collection) {
            RTC_LOG(LS_ERROR << "Device collection not valid");
            return -1;
        }

        UINT count = 0;
        hr = collection->GetCount(&count);
        if (FAILED(hr)) {
            _TraceCOMError(hr);
            return -1;
        }

        *index = -1;
        for (UINT i = 0; i < count; i++) {
            memset(szDeviceID, 0, sizeof(szDeviceID));
            CComPtr<IMMDevice> device;
            {
                IMMDevice* ptrDevice = NULL;
                hr = collection->Item(i, &ptrDevice);
                if (FAILED(hr) || ptrDevice == NULL) {
                    _TraceCOMError(hr);
                    return -1;
                }
                device = ptrDevice;
                SAFE_RELEASE(ptrDevice);
            }

            if (_GetDeviceID(device, szDeviceID, kDeviceIDLength) == -1) {
                return -1;
            }

            if (wcsncmp(szDefaultDeviceID, szDeviceID, kDeviceIDLength) == 0) {
                // Found a match.
                *index = i;
                break;
            }
        }

        if (*index == -1) {
            RTC_LOG(LS_ERROR << "Unable to find collection index for default device");
            return -1;
        }

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  _GetDeviceName
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetDeviceName(IMMDevice* pDevice,
        LPWSTR pszBuffer,
        int bufferLen) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        static const WCHAR szDefault[] = L"<Device not available>";

        HRESULT hr = E_FAIL;
        IPropertyStore* pProps = NULL;
        PROPVARIANT varName;

        assert(pszBuffer != NULL);
        assert(bufferLen > 0);

        if (pDevice != NULL) {
            hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
            if (FAILED(hr)) {
                RTC_LOG(LS_ERROR << "IMMDevice::OpenPropertyStore failed, hr = 0x" << std::hex << hr << std::dec);
            }
        }

        // Initialize container for property value.
        PropVariantInit(&varName);

        if (SUCCEEDED(hr)) {
            // Get the endpoint device's friendly-name property.
            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            if (FAILED(hr)) {
                RTC_LOG(LS_ERROR << "IPropertyStore::GetValue failed, hr = 0x" << std::hex << hr << std::dec);
            }
        }

        if ((SUCCEEDED(hr)) && (VT_EMPTY == varName.vt)) {
            hr = E_FAIL;
            RTC_LOG(LS_ERROR << "IPropertyStore::GetValue returned no value," << " hr = 0x" << std::hex << hr << std::dec);
        }

        if ((SUCCEEDED(hr)) && (VT_LPWSTR != varName.vt)) {
            // The returned value is not a wide null terminated string.
            hr = E_UNEXPECTED;
            RTC_LOG(LS_ERROR << "IPropertyStore::GetValue returned unexpected)" << " type, hr = 0x" << std::hex << hr << std::dec);
        }

        if (SUCCEEDED(hr) && (varName.pwszVal != NULL)) {
            // Copy the valid device name to the provided ouput buffer.
            wcsncpy_s(pszBuffer, bufferLen, varName.pwszVal, _TRUNCATE);
        }
        else {
            // Failed to find the device name.
            wcsncpy_s(pszBuffer, bufferLen, szDefault, _TRUNCATE);
        }

        PropVariantClear(&varName);
        SAFE_RELEASE(pProps);

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  _GetDeviceID
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetDeviceID(IMMDevice* pDevice,
        LPWSTR pszBuffer,
        int bufferLen) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        static const WCHAR szDefault[] = L"<Device not available>";

        HRESULT hr = E_FAIL;
        LPWSTR pwszID = NULL;

        assert(pszBuffer != NULL);
        assert(bufferLen > 0);

        if (pDevice != NULL) {
            hr = pDevice->GetId(&pwszID);
        }

        if (hr == S_OK) {
            // Found the device ID.
            wcsncpy_s(pszBuffer, bufferLen, pwszID, _TRUNCATE);
        }
        else {
            // Failed to find the device ID.
            wcsncpy_s(pszBuffer, bufferLen, szDefault, _TRUNCATE);
        }

        CoTaskMemFree(pwszID);
        return 0;
    }

    // ----------------------------------------------------------------------------
    //  _GetDefaultDevice
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetDefaultDevice(EDataFlow dir,
        ERole role,
        IMMDevice** ppDevice) {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        HRESULT hr(S_OK);

        assert(m_ptrEnumerator != NULL);

        hr = m_ptrEnumerator->GetDefaultAudioEndpoint(dir, role, ppDevice);
        if (FAILED(hr)) {
            _TraceCOMError(hr);
            return -1;
        }

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  _GetListDevice
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_GetListDevice(EDataFlow dir,
        int index,
        IMMDevice** ppDevice) {
        HRESULT hr(S_OK);

        assert(m_ptrEnumerator != NULL);

        IMMDeviceCollection* pCollection = NULL;

        hr = m_ptrEnumerator->EnumAudioEndpoints(
            dir,
            DEVICE_STATE_ACTIVE,  // only active endpoints are OK
            &pCollection);
        if (FAILED(hr)) {
            _TraceCOMError(hr);
            SAFE_RELEASE(pCollection);
            return -1;
        }

        hr = pCollection->Item(index, ppDevice);
        if (FAILED(hr)) {
            _TraceCOMError(hr);
            SAFE_RELEASE(pCollection);
            return -1;
        }

        return 0;
    }

    // ----------------------------------------------------------------------------
    //  _EnumerateEndpointDevicesAll
    // ----------------------------------------------------------------------------

    int32_t AudioDeviceWindowsCore::_EnumerateEndpointDevicesAll(
        EDataFlow dataFlow) const {
        RTC_LOG(LS_VERBOSE << __FUNCTION__);

        assert(m_ptrEnumerator != NULL);

        HRESULT                 hr              = S_OK;
        IMMDeviceCollection*    pCollection     = NULL;
        IMMDevice*              pEndpoint       = NULL;
        IPropertyStore*         pProps          = NULL;
        IAudioEndpointVolume*   pEndpointVolume = NULL;
        LPWSTR                  pwszID          = NULL;

        // Generate a collection of audio endpoint devices in the system.
        // Get states for *all* endpoint devices.
        // Output: IMMDeviceCollection interface.
        hr = m_ptrEnumerator->EnumAudioEndpoints( dataFlow, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &pCollection);

        EXIT_ON_ERROR(hr);

        // use the IMMDeviceCollection interface...

        UINT count/*(0)*/;

        // Retrieve a count of the devices in the device collection.
        EXIT_ON_ERROR(hr = pCollection->GetCount(&count));

        if (dataFlow == eRender)
        {
            RTC_LOG(LS_VERBOSE << "#rendering endpoint devices (counting all): " << count);
        }
        else if (dataFlow == eCapture)
        {
            RTC_LOG(LS_VERBOSE << "#capturing endpoint devices (counting all): " << count);
        }

        if (count == 0)
            return 0;

        // Each loop prints the name of an endpoint device.
        for (ULONG i = 0; i < count; i++) {
            RTC_LOG(LS_VERBOSE << "Endpoint " << i << ":");

            // Get pointer to endpoint number i.
            // Output: IMMDevice interface.
            CONTINUE_ON_ERROR(hr = pCollection->Item(i, &pEndpoint));

            // use the IMMDevice interface of the specified endpoint device...

            // Get the endpoint ID string (uniquely identifies the device among all
            // audio endpoint devices)
            CONTINUE_ON_ERROR(hr = pEndpoint->GetId(&pwszID));

            RTC_LOG(LS_VERBOSE << "ID string    : " << pwszID);

            // Retrieve an interface to the device's property store.
            // Output: IPropertyStore interface.
            hr = pEndpoint->OpenPropertyStore(STGM_READ, &pProps);
            CONTINUE_ON_ERROR(hr);

            // use the IPropertyStore interface...

            PROPVARIANT varName;
            // Initialize container for property value.
            PropVariantInit(&varName);

            // Get the endpoint's friendly-name property.
            // Example: "Speakers (Realtek High Definition Audio)"
            CONTINUE_ON_ERROR(hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName));

            RTC_LOG(LS_VERBOSE << "friendly name: \"" << varName.pwszVal << "\"");

            // Get the endpoint's current device state
            DWORD dwState;
            CONTINUE_ON_ERROR(hr = pEndpoint->GetState(&dwState));

            if (dwState & DEVICE_STATE_ACTIVE)
                RTC_LOG(LS_VERBOSE << "state (0x" << std::hex << dwState << std::dec << ")  : *ACTIVE*");
            if (dwState & DEVICE_STATE_DISABLED)
                RTC_LOG(LS_VERBOSE << "state (0x" << std::hex << dwState << std::dec << ")  : DISABLED");
            if (dwState & DEVICE_STATE_NOTPRESENT)
                RTC_LOG(LS_VERBOSE << "state (0x" << std::hex << dwState << std::dec << ")  : NOTPRESENT");
            if (dwState & DEVICE_STATE_UNPLUGGED)
                RTC_LOG(LS_VERBOSE << "state (0x" << std::hex << dwState << std::dec << ")  : UNPLUGGED");

            // Check the hardware volume capabilities.
            DWORD dwHwSupportMask/*(0)*/;
            CONTINUE_ON_ERROR(hr = pEndpoint->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pEndpointVolume));
            
            CONTINUE_ON_ERROR(hr = pEndpointVolume->QueryHardwareSupport(&dwHwSupportMask));
            
            if (dwHwSupportMask & ENDPOINT_HARDWARE_SUPPORT_VOLUME) // The audio endpoint device supports a hardware volume control
                RTC_LOG(LS_VERBOSE << "hwmask (0x" << std::hex << dwHwSupportMask << std::dec << ") : HARDWARE_SUPPORT_VOLUME");
            if (dwHwSupportMask & ENDPOINT_HARDWARE_SUPPORT_MUTE) // The audio endpoint device supports a hardware mute control
                RTC_LOG(LS_VERBOSE << "hwmask (0x" << std::hex << dwHwSupportMask << std::dec << ") : HARDWARE_SUPPORT_MUTE");
            if (dwHwSupportMask & ENDPOINT_HARDWARE_SUPPORT_METER) // The audio endpoint device supports a hardware peak meter
                RTC_LOG(LS_VERBOSE << "hwmask (0x" << std::hex << dwHwSupportMask << std::dec << ") : HARDWARE_SUPPORT_METER");

            // Check the channel count (#channels in the audio stream that enters or
            // leaves the audio endpoint device)
            UINT nChannelCount/*(0)*/;
            hr = pEndpointVolume->GetChannelCount(&nChannelCount);
            CONTINUE_ON_ERROR(hr);
            
            RTC_LOG(LS_VERBOSE << "#channels    : " << nChannelCount);

            if (dwHwSupportMask & ENDPOINT_HARDWARE_SUPPORT_VOLUME) 
            {
                // Get the volume range.
                float fLevelMinDB(0.0);
                float fLevelMaxDB(0.0);
                float fVolumeIncrementDB(0.0);
                
                CONTINUE_ON_ERROR(hr = pEndpointVolume->GetVolumeRange(&fLevelMinDB, &fLevelMaxDB, &fVolumeIncrementDB));

                RTC_LOG(LS_VERBOSE << "volume range : " << fLevelMinDB << " (min), " << fLevelMaxDB << " (max), " << fVolumeIncrementDB << " (inc) [dB]");

                // The volume range from vmin = fLevelMinDB to vmax = fLevelMaxDB is
                // divided into n uniform intervals of size vinc = fVolumeIncrementDB,
                // where n = (vmax ?vmin) / vinc. The values vmin, vmax, and vinc are
                // measured in decibels. The client can set the volume level to one of n +
                // 1 discrete values in the range from vmin to vmax.
                int n = (int)((fLevelMaxDB - fLevelMinDB) / fVolumeIncrementDB);
                RTC_LOG(LS_VERBOSE << "#intervals   : " << n);

                // Get information about the current step in the volume range.
                // This method represents the volume level of the audio stream that enters
                // or leaves the audio endpoint device as an index or "step" in a range of
                // discrete volume levels. Output value nStepCount is the number of steps
                // in the range. Output value nStep is the step index of the current
                // volume level. If the number of steps is n = nStepCount, then step index
                // nStep can assume values from 0 (minimum volume) to n ?1 (maximum
                // volume).
                UINT nStep/*(0)*/;
                UINT nStepCount/*(0)*/;
                
                CONTINUE_ON_ERROR(hr = pEndpointVolume->GetVolumeStepInfo(&nStep, &nStepCount));

                RTC_LOG(LS_VERBOSE << "volume steps : " << nStep << " (nStep), " << nStepCount << " (nStepCount)");
            }
        Next:
            if (FAILED(hr))
                RTC_LOG(LS_VERBOSE << "Error when logging device information");

            CoTaskMemFree(pwszID);
            pwszID = NULL;
            PropVariantClear(&varName);

            SAFE_RELEASE(pProps);
            SAFE_RELEASE(pEndpoint);
            SAFE_RELEASE(pEndpointVolume);
        }
        SAFE_RELEASE(pCollection);
        return 0;

    Exit:
        _TraceCOMError(hr);
        CoTaskMemFree(pwszID);
        pwszID = NULL;
        SAFE_RELEASE(pCollection);
        SAFE_RELEASE(pEndpoint);
        SAFE_RELEASE(pEndpointVolume);
        SAFE_RELEASE(pProps);
        return -1;
    }

    // ----------------------------------------------------------------------------
    //  _TraceCOMError
    // ----------------------------------------------------------------------------

    void AudioDeviceWindowsCore::_TraceCOMError(HRESULT hr) const {
        TCHAR buf[MAXERRORLENGTH];
        TCHAR errorText[MAXERRORLENGTH];

        const DWORD dwFlags     = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD dwLangID    = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

        // Gets the system's human readable message string for this HRESULT.
        // All error message in English by default.
        DWORD messageLength = ::FormatMessageW(dwFlags, 0, hr, dwLangID, errorText, MAXERRORLENGTH, NULL);

        assert(messageLength <= MAXERRORLENGTH);

        // Trims tailing white space (FormatMessage() leaves a trailing cr-lf.).
        for (; messageLength && ::isspace(errorText[messageLength - 1]); --messageLength)
            errorText[messageLength - 1] = '\0';

        RTC_LOG(LS_ERROR << "Core Audio method failed (hr=" << hr << ")");

        StringCchPrintf(buf, MAXERRORLENGTH, TEXT("Error details: "));
        StringCchCat(buf, MAXERRORLENGTH, errorText);
        RTC_LOG(LS_ERROR << WideToUTF8(buf));
    }

    // ----------------------------------------------------------------------------
    //  WideToUTF8
    // ----------------------------------------------------------------------------

    char* AudioDeviceWindowsCore::WideToUTF8(const TCHAR* src) const {
#ifdef UNICODE
        const size_t kStrLen = sizeof(m_str);
        memset(m_str, 0, kStrLen);
        // Get required size (in bytes) to be able to complete the conversion.
        unsigned int required_size =
            (unsigned int)WideCharToMultiByte(CP_UTF8, 0, src, -1, m_str, 0, 0, 0);
        if (required_size <= kStrLen) {
            // Process the entire input string, including the terminating null char.
            if (WideCharToMultiByte(CP_UTF8, 0, src, -1, m_str, kStrLen, 0, 0) == 0)
                memset(m_str, 0, kStrLen);
        }
        return m_str;
#else
        return const_cast<char*>(src);
#endif
    }

    bool AudioDeviceWindowsCore::KeyPressed() const {
        int key_down = 0;
        for (int key = VK_SPACE; key < VK_NUMLOCK; key++) {
            short res = GetAsyncKeyState(key);
            key_down |= res & 0x1;  // Get the LSB
        }
        return (key_down > 0);
    }
}  // namespace webrtc

//#endif  // WEBRTC_WINDOWS_CORE_AUDIO_BUILD
