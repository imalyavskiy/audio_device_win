
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

const uint32_t riff_4cc = MAKEFOURCC('R', 'I', 'F', 'F');
const uint32_t wave_4cc = MAKEFOURCC('W', 'A', 'V', 'E');
const uint32_t fmt_4cc  = MAKEFOURCC('f', 'm', 't', ' ');
const uint32_t data_4cc = MAKEFOURCC('d', 'a', 't', 'a');

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


    ChunkDescriptor riff_descriptor{ 0 };
    while(m_source_data.rdstate() != std::ios_base::eofbit )
    {
        m_source_data.read(reinterpret_cast<char*>(&riff_descriptor), sizeof(ChunkDescriptor));
        if (m_source_data.rdstate() != std::ios_base::goodbit)
            break;

        const std::streampos riff_payload_begin = m_source_data.tellg();
        const std::streampos riff_payload_end   = riff_payload_begin + std::streampos(riff_descriptor.size);

        uint32_t riff_type = 0;
        m_source_data.read(reinterpret_cast<char*>(&riff_type), 4);
        if (m_source_data.rdstate() != std::ios_base::goodbit)
            break;

        if (riff_4cc == riff_descriptor.fourcc || wave_4cc == riff_type)
        {
            if (FAILED(ReadWafeRiff(riff_payload_begin, riff_payload_end, m_wave_riff)));
            {
                m_source_data.seekg(riff_payload_end);
                continue;
            }
        }
        else
        {
            m_source_data.seekg(riff_descriptor.size - 4, std::ios_base::cur); // -4 means except 4 bytes of the riff type FOURCC
        }
    }

    return (bool)m_wave_riff;
}

HRESULT 
MyAudioSourceImpl::ReadWafeRiff(const std::streampos& begin, const std::streampos& end, std::unique_ptr<WaveRiff>& wave_riff)
{
    HRESULT hr = S_OK;
    std::streampos tmp = begin;
    wave_riff.reset();
    
    std::unique_ptr<WaveRiff> riff(new WaveRiff);

    if (m_source_data.tellg() != begin)
        m_source_data.seekg(begin);

    uint32_t type = 0;
    m_source_data.read(reinterpret_cast<char*>(&type), 4);
    tmp = m_source_data.tellg();

    if (m_source_data.rdstate() & std::ios_base::failbit || wave_4cc != type) 
        return E_FAIL;

    while(m_source_data.tellg() < end)
    {
        ChunkDescriptor chunk_descriptor{ 0 };
        m_source_data.read(reinterpret_cast<char*>(&chunk_descriptor), sizeof(ChunkDescriptor));
        tmp = m_source_data.tellg();
        if (m_source_data.rdstate() & std::ios_base::failbit)
            return E_FAIL;

        if (chunk_descriptor.fourcc == fmt_4cc)
        {
            hr = ReadFMTChunk(m_source_data.tellg(), chunk_descriptor, riff->format);
            if (FAILED(hr))
                return hr;
        }
        else if (chunk_descriptor.fourcc == data_4cc)
        {
            hr = ReadDataChunk(m_source_data.tellg(), chunk_descriptor, riff->data);
            if (FAILED(hr))
                return hr;
        }
        else
        {
            //m_source_data.seekg(chunk_descriptor.size, std::ios_base::cur); // skip unexpected chunks
            return E_FAIL; // we are expecting only 'fmt ' chunk followed by 'data' chunk
        }
    }
    
    riff.swap(wave_riff);
    
    return S_OK;
}

HRESULT 
MyAudioSourceImpl::ReadFMTChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<FmtChunk>& fmt_chunk)
{
    fmt_chunk.reset();

    if (chunk_descr.size != 16 && chunk_descr.size != 18 && chunk_descr.size != 40)
        return E_INVALIDARG;

    std::unique_ptr<FmtChunk> fmt(new FmtChunk);
    ZeroMemory(fmt.get(), sizeof(FmtChunk));

    if (chunk_descr.size == 16)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk16*>(fmt.get())), sizeof(FmtChunk16));
    else if (chunk_descr.size == 18)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk18*>(fmt.get())), sizeof(FmtChunk18));
    else if (chunk_descr.size == 40)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk40*>(fmt.get())), sizeof(FmtChunk40));

    if ((begin + std::streampos(chunk_descr.size)) != m_source_data.tellg())
        return E_FAIL;

    fmt.swap(fmt_chunk);

    return S_OK;
}

HRESULT 
MyAudioSourceImpl::ReadDataChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<DataChunk>& data_chunk)
{
    data_chunk.reset();
    std::unique_ptr<DataChunk> chunk(new DataChunk);
    chunk->chunk_begin = begin;
    chunk->chunk_end = begin + std::streampos(chunk_descr.size);

    m_source_data.seekg(chunk->chunk_end);

    chunk.swap(data_chunk);

    return S_OK;
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
