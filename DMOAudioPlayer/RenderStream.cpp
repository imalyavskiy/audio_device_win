
#include "stdafx.h"
#include "RenderStream.h"

#include <wmcodecdsp.h>      // CLSID_CWMAudioAEC
// (must be before audioclient.h)
#include <Audioclient.h>     // WASAPI
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>     // MMDevice
#include <avrt.h>            // Avrt
#include <endpointvolume.h>
#include <mediaobj.h>        // IMediaObject

//-----------------------------------------------------------
// Play an audio stream on the default audio rendering
// device. The PlayAudioStream function allocates a shared
// buffer big enough to hold one second of PCM audio data.
// The function uses this buffer to stream data to the
// rendering device. The inner loop runs every 1/2 second.
//-----------------------------------------------------------

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator 
    = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator 
    = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient 
    = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient 
    = __uuidof(IAudioRenderClient);

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         0x0001
#endif

HRESULT PlayAudioStream(MyAudioSource *pMySource)
{
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
    REFERENCE_TIME hnsActualDuration;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioClient *pAudioClient = NULL;
    IAudioRenderClient *pRenderClient = NULL;
    WAVEFORMATEX *pwfx = NULL;
    WAVEFORMATEXTENSIBLE* pwfext = NULL;
    UINT32 bufferFrameCount;
    UINT32 numFramesAvailable;
    UINT32 numFramesPadding;
    BYTE *pData;
    DWORD flags = 0;

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    EXIT_ON_ERROR(hr)

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr)

    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr)

    {
        WAVEFORMATEX desired_wfx;
        ZeroMemory(&desired_wfx, sizeof(WAVEFORMATEX));

        desired_wfx.wFormatTag = WAVE_FORMAT_PCM;           // WORD        wFormatTag;         /* format type */
        desired_wfx.nChannels = 2;                          // WORD        nChannels;          /* number of channels (i.e. mono; stereo...) */
        desired_wfx.nSamplesPerSec = 48000;                 // DWORD       nSamplesPerSec;     /* sample rate */
        desired_wfx.nAvgBytesPerSec = 192000/*384000*/;     // DWORD       nAvgBytesPerSec;    /* for buffer estimation */
        desired_wfx.nBlockAlign = 4/*8*/;                   // WORD        nBlockAlign;        /* block size of data */
        desired_wfx.wBitsPerSample = 16/*32*/;              // WORD        wBitsPerSample;     /* number of bits per sample of mono data */
        desired_wfx.cbSize = 0;                             // WORD        cbSize;             /* the count in bytes of the size of */
                                                            //                                 /* extra information (after cbSize) */
        WAVEFORMATEX* closest_wfx;
        closest_wfx = NULL;

        hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &desired_wfx, &closest_wfx);
        if (hr == S_FALSE)
            hr = E_FAIL;
        EXIT_ON_ERROR(hr)

        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, &desired_wfx, NULL);
        EXIT_ON_ERROR(hr)

        // Tell the audio source which format to use.
//        hr = pMySource->SetFormat(&desired_wfx);
        EXIT_ON_ERROR(hr)
    }


    // Get the actual size of the allocated buffer.
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetService(IID_IAudioRenderClient, (void**)&pRenderClient);
    EXIT_ON_ERROR(hr)

    // Grab the entire buffer for the initial fill operation.
    hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
    EXIT_ON_ERROR(hr)

        // Load the initial data into the shared buffer.
    hr = pMySource->LoadData(bufferFrameCount, pData, &flags);
    EXIT_ON_ERROR(hr)

    hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
    EXIT_ON_ERROR(hr)

    // Calculate the actual duration of the allocated buffer.
    hnsActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

    hr = pAudioClient->Start();  // Start playing.
    EXIT_ON_ERROR(hr)

    // Each loop fills about half of the shared buffer.
    while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
    {
        // Sleep for half the buffer duration.
        Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

        // See how much buffer space is available.
        hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
        EXIT_ON_ERROR(hr)

        numFramesAvailable = bufferFrameCount - numFramesPadding;

        // Grab all the available space in the shared buffer.
        hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
        EXIT_ON_ERROR(hr)

        // Get next 1/2-second of data from the audio source.
        hr = pMySource->LoadData(numFramesAvailable, pData, &flags);
        EXIT_ON_ERROR(hr)

        hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags);
        EXIT_ON_ERROR(hr)
    }

    // Wait for last data in buffer to play before stopping.
    Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

    hr = pAudioClient->Stop();  // Stop playing.
    EXIT_ON_ERROR(hr)

Exit:
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pRenderClient);

    return hr;
}

std::shared_ptr<MyAudioSource>
MyAudioSourceImpl::Create(const std::string& file)
{
    MyAudioSourceImpl* p = new MyAudioSourceImpl();

    if (!p->Init(file))
        return {};

    return std::shared_ptr<MyAudioSource>(p);
}

MyAudioSourceImpl::MyAudioSourceImpl()
{

}

bool MyAudioSourceImpl::Init(const std::string& file)
{
	// http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
    m_source_data.open(file, std::ios_base::in | std::ios_base::binary);
    assert(m_source_data.is_open());

    const std::streampos begin = m_source_data.tellg();

    m_source_data.seekg(0, std::ios_base::end);

    const std::streampos end = m_source_data.tellg();

    m_source_data.seekg(0, std::ios_base::beg);

    m_file_size = end - begin;

    DWORD dwRIFF = 0;
    m_source_data.read(reinterpret_cast<char*>(&dwRIFF), sizeof(DWORD));
    assert(MAKEFOURCC('R', 'I', 'F', 'F', ) == dwRIFF);

    DWORD dwRIFFSize = 0;
    m_source_data.read(reinterpret_cast<char*>(&dwRIFFSize), sizeof(DWORD));
    assert(dwRIFFSize == end - m_source_data.tellg());

    DWORD dwWAVE = 0;
    m_source_data.read(reinterpret_cast<char*>(&dwWAVE), sizeof(DWORD));
    assert(MAKEFOURCC('W', 'A', 'V', 'E', ) == dwWAVE);

    DWORD dwChunkId = 0;
    m_source_data.read(reinterpret_cast<char*>(&dwChunkId), sizeof(DWORD));
    if (MAKEFOURCC('f', 'm', 't', ' ', ) == dwChunkId)
    {
        for (;;) {
            DWORD   chkSize = 0;
            m_source_data.read(reinterpret_cast<char*>(&chkSize), sizeof(DWORD));
            assert(chkSize == 16 || chkSize == 18 || chkSize == 40);

            WORD    wFormatTag = 0; // 2(2)
            m_source_data.read(reinterpret_cast<char*>(&wFormatTag), sizeof(WORD));

            WORD    nChannels = 0;  // 2(4)
            m_source_data.read(reinterpret_cast<char*>(&nChannels), sizeof(WORD));

            DWORD   nSamplesPerSec = 0; // 4(8)
            m_source_data.read(reinterpret_cast<char*>(&nSamplesPerSec), sizeof(DWORD));

            DWORD   nAvgBytesPerSec = 0; // 4(12)
            m_source_data.read(reinterpret_cast<char*>(&nAvgBytesPerSec), sizeof(DWORD));

            WORD    nBlockAlign = 0; // 2(14)
            m_source_data.read(reinterpret_cast<char*>(&nBlockAlign), sizeof(WORD));

            WORD    wBitsPerSample = 0; // 2(16)
            m_source_data.read(reinterpret_cast<char*>(&wBitsPerSample), sizeof(WORD));

            if (chkSize == 16) break;

            WORD    cbSize = 0; // 2(18)
            m_source_data.read(reinterpret_cast<char*>(&dwRIFFSize), sizeof(WORD));

            if (chkSize == 18) break;

            WORD    wValidBitsPerSample = 0; // 2(20)
            m_source_data.read(reinterpret_cast<char*>(&cbSize), sizeof(WORD));

            DWORD   dwChannelMask = 0;	// 4(24)
            m_source_data.read(reinterpret_cast<char*>(&dwChannelMask), sizeof(DWORD));

            GUID    SubFormat{ 0 };	// 16(40)
            m_source_data.read(reinterpret_cast<char*>(&SubFormat), sizeof(GUID));

            assert(chkSize == 40);
            
            break;
        }
    }

    return false;
}

MyAudioSourceImpl::~MyAudioSourceImpl()
{
//     char* p = reinterpret_cast<char*>(m_pwfx);
//     delete[] p;
//     m_pwfx = NULL;
}

HRESULT 
MyAudioSourceImpl::GetFormat(std::unique_ptr<WAVEFORMATEX>& pwfx)
{
    if (!pwfx)
        return E_INVALIDARG;

    pwfx.release();
    pwfx = std::unique_ptr<WAVEFORMATEX>(new WAVEFORMATEX);

//    assert(0 == m_file_size % ((m_pwfx->wBitsPerSample * m_pwfx->nChannels) / 8));

    return S_OK;
}

HRESULT 
MyAudioSourceImpl::LoadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags)
{
//     std::streampos file_bytes_rest = m_file_size - m_source_data.tellg();
//     std::streampos curr_pos;
//     
//     const uint32_t size = bufferFrameCount * ((m_pwfx->nChannels * m_pwfx->wBitsPerSample) / 8);
// 
//     if (size < file_bytes_rest)
//     {
//         m_source_data.read(reinterpret_cast<char*>(pData), std::streamsize(size));
// 
//         if (std::ios_base::failbit & m_source_data.rdstate())
//             return E_FAIL;
// 
//         curr_pos = m_source_data.tellg();
// 
//         return S_OK;
//     }

    return E_FAIL;

}
