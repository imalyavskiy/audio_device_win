#ifndef __PLAY_AUDIO_STREAM_H__
#define __PLAY_AUDIO_STREAM_H__
#pragma once

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC        10000000
#define REFTIMES_PER_MILLISEC   10000

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         0x0001
#endif

#define USE_PCM_DATA_BUFFER TRUE


class PlayAudioStreamClass
{
public:
    PlayAudioStreamClass();
    ~PlayAudioStreamClass();

    HRESULT operator()(WavAudioSource::Interface *pMySource);

protected:
    HRESULT Init(WavAudioSource::Interface *pMySource);

    HRESULT Do(WavAudioSource::Interface *pMySource);

protected:
    HRESULT                       hr = S_OK;

    CComPtr<IMMDeviceEnumerator>        pEnumerator;
    CComPtr<IMMDevice>                  pDevice;
    CComPtr<IAudioClient>               pAudioClient;
    CComPtr<IAudioRenderClient>         pRenderClient;

    ComUniquePtr<WAVEFORMATEX>          p_mix_format;
    ComUniquePtr<WAVEFORMATEXTENSIBLE>  p_desired_format;
    ComUniquePtr<WAVEFORMATEX>          p_closest_format;

    REFERENCE_TIME                      rtRequestedDuration = REFTIMES_PER_SEC;
    REFERENCE_TIME                      rtActualDuration = 0;

    UINT32                              bufferFrameCount = 0;
    UINT32                              numFramesAvailable = 0;
    UINT32                              numFramesPadding = 0;

    BYTE*                               pData = NULL;
    DWORD                               flags = 0;

    ScopedCOMInitializer                com_guard;

    //
    PCMFormat                           format{ 0, 0, 0, 0 };

};

HRESULT PlayAudioStream(WavAudioSource::Interface *pMySource);

#endif // __PLAY_AUDIO_STREAM_H__
