#ifndef __AUDIO_SOURCE_INTERFACE_H__
#define __AUDIO_SOURCE_INTERFACE_H__
#pragma once
struct IWavAudioSource
{
    typedef std::shared_ptr<IWavAudioSource> ptr;

    virtual ~IWavAudioSource() {};

    virtual bool GetFormat(PCMFormat& format) = 0;
    virtual bool ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) = 0;
    virtual bool ReadData(std::shared_ptr<PCMDataBuffer> buffer) = 0;
};

bool create(const std::string& file, std::shared_ptr<IWavAudioSource>& source);
#endif // __AUDIO_SOURCE_INTERFACE_H__