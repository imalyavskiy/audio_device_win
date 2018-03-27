#include "stdafx.h"
#include "common.h"
#include "AudioSourceInterface.h"
#include "PlayAudioStream.h"

PlayAudioStreamClass::PlayAudioStreamClass()
    : p_mix_format(nullptr, &CoTaskMemFree)
    , p_desired_format(nullptr, &CoTaskMemFree)
    , p_closest_format(nullptr, &CoTaskMemFree)
{

}

PlayAudioStreamClass::~PlayAudioStreamClass()
{

}

HRESULT 
PlayAudioStreamClass::operator()(WavAudioSource::Interface *pMySource)
{
  
    std::thread init(std::bind(&PlayAudioStreamClass::Init, this, std::placeholders::_1), pMySource);

    init.join();

    std::thread do_(std::bind(&PlayAudioStreamClass::Do, this, std::placeholders::_1), pMySource);

    do_.join();

    return hr;
}

HRESULT PlayAudioStreamClass::Init(WavAudioSource::Interface *pMySource)
{
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

    if (FAILED(hr = pMySource->GetFormat(format)))
        return hr;

    {   // check if the proposed format can be consumed
        p_desired_format = std::unique_ptr<WAVEFORMATEXTENSIBLE, decltype(&CoTaskMemFree)>((WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE)), &CoTaskMemFree);
        p_desired_format->Format.wFormatTag = 0xfffe;
        p_desired_format->Format.nChannels = format.channels;
        p_desired_format->Format.nSamplesPerSec = format.samplesPerSecond;
        p_desired_format->Format.nAvgBytesPerSec = format.samplesPerSecond * format.bytesPerFrame;
        p_desired_format->Format.nBlockAlign = format.bytesPerFrame;
        p_desired_format->Format.wBitsPerSample = format.bitsPerSample;
        p_desired_format->Format.cbSize = 22;
        p_desired_format->Samples.wValidBitsPerSample = format.bitsPerSample;
        p_desired_format->dwChannelMask = 0x3;
        p_desired_format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;


        WAVEFORMATEX* tmp;
        hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, reinterpret_cast<WAVEFORMATEX*>(p_desired_format.get()), &tmp);
        if (FAILED(hr))
            return hr;

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

    return hr;
}

HRESULT PlayAudioStreamClass::Do(WavAudioSource::Interface *pMySource)
{
    // Grab the entire buffer for the initial fill operation.
    if (FAILED(hr = pRenderClient->GetBuffer(bufferFrameCount, &pData)))
        return hr;

#if !USE_PCM_DATA_BUFFER
    // Load the initial data into the shared buffer.
    if (FAILED(hr = pMySource->ReadData(bufferFrameCount, pData, &flags)))
        return hr;
#else
    //
    PCMDataBuffer::sptr pBuffer(new PCMDataBuffer{ pData, bufferFrameCount * format.bytesPerFrame, 0, false });

    //
    if (FAILED(hr = pMySource->ReadData(pBuffer)))
        return hr;
#endif // USE_PCM_DATA_BUFFER

    if (FAILED(hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags)))
        return hr;

    // Calculate the actual duration of the allocated buffer.
    rtActualDuration = (REFERENCE_TIME)(double)REFTIMES_PER_SEC * bufferFrameCount / p_desired_format->Format.nSamplesPerSec;

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
#if !USE_PCM_DATA_BUFFER
        // Get next 1/2-second of data from the audio source.
        if (FAILED(hr = pMySource->ReadData(numFramesAvailable, pData, &flags)))
            return hr;
#else
        *pBuffer = PCMDataBuffer{ pData, numFramesAvailable * format.bytesPerFrame, 0, false };

        //
        if (!pMySource->ReadData(pBuffer))
            return E_FAIL;
#endif // USE_PCM_DATA_BUFFER

        if (FAILED(hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags)))
            return hr;
    }

    // Wait for last data in buffer to play before stopping.
    Sleep((DWORD)(rtActualDuration / REFTIMES_PER_MILLISEC / 2));

    if (FAILED(hr = pAudioClient->Stop()))  // Stop playing.
        return hr;

    return hr;
}


HRESULT PlayAudioStream(WavAudioSource::Interface *pMySource)
{
    HRESULT                 hr = S_OK;

    CComPtr<IMMDeviceEnumerator>  pEnumerator;
    CComPtr<IMMDevice>            pDevice;
    CComPtr<IAudioClient>         pAudioClient;
    CComPtr<IAudioRenderClient>   pRenderClient;

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
    PCMFormat format{ 0, 0, 0, 0 };

    if (FAILED(hr = pMySource->GetFormat(format)))
        return hr;

    {   // check if the proposed format can be consumed
        p_desired_format = std::unique_ptr<WAVEFORMATEXTENSIBLE, decltype(&CoTaskMemFree)>((WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE)), &CoTaskMemFree);
        p_desired_format->Format.wFormatTag             = 0xfffe;
        p_desired_format->Format.nChannels              = format.channels;
        p_desired_format->Format.nSamplesPerSec         = format.samplesPerSecond;
        p_desired_format->Format.nAvgBytesPerSec        = format.samplesPerSecond * format.bytesPerFrame;
        p_desired_format->Format.nBlockAlign            = format.bytesPerFrame;
        p_desired_format->Format.wBitsPerSample         = format.bitsPerSample;
        p_desired_format->Format.cbSize                 = 22;
        p_desired_format->Samples.wValidBitsPerSample   = format.bitsPerSample;
        p_desired_format->dwChannelMask                 = 0x3;
        p_desired_format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;


        WAVEFORMATEX* tmp;
        hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, reinterpret_cast<WAVEFORMATEX*>(p_desired_format.get()), &tmp);
        if (FAILED(hr))
            return hr;

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

#if !USE_PCM_DATA_BUFFER
    // Load the initial data into the shared buffer.
    if (FAILED(hr = pMySource->ReadData(bufferFrameCount, pData, &flags)))
        return hr;
#else
    //
    PCMDataBuffer::sptr pBuffer(new PCMDataBuffer{ pData, bufferFrameCount * format.bytesPerFrame, 0, false });

    //
    if(FAILED(hr = pMySource->ReadData(pBuffer)))
        return hr;
#endif // USE_PCM_DATA_BUFFER

    if (FAILED(hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags)))
        return hr;

    // Calculate the actual duration of the allocated buffer.
    rtActualDuration = (REFERENCE_TIME)(double)REFTIMES_PER_SEC * bufferFrameCount / p_desired_format->Format.nSamplesPerSec;

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
#if !USE_PCM_DATA_BUFFER
        // Get next 1/2-second of data from the audio source.
        if (FAILED(hr = pMySource->ReadData(numFramesAvailable, pData, &flags)))
            return hr;
#else
        *pBuffer = PCMDataBuffer{ pData, numFramesAvailable * format.bytesPerFrame, 0, false };

        //
        if (FAILED(hr = pMySource->ReadData(pBuffer)))
            return hr;
#endif // USE_PCM_DATA_BUFFER

        if (FAILED(hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags)))
            return hr;
    }

    // Wait for last data in buffer to play before stopping.
    Sleep((DWORD)(rtActualDuration / REFTIMES_PER_MILLISEC / 2));

    if (FAILED(hr = pAudioClient->Stop()))  // Stop playing.
        return hr;

    return hr;
}