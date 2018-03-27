#ifndef __AUDIO_SOURCE_INTERFACE_H__
#define __AUDIO_SOURCE_INTERFACE_H__
#pragma once
namespace WavAudioSource
{
    struct Interface
    {
        typedef std::shared_ptr<Interface> ptr;

        virtual ~Interface() {};

        virtual bool GetFormat(PCMFormat& format) = 0;
        virtual bool ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) = 0;
        virtual bool ReadData(std::shared_ptr<PCMDataBuffer> buffer) = 0;
    };

    bool create(const std::string& file, std::shared_ptr<Interface>& source);
}

#endif // __AUDIO_SOURCE_INTERFACE_H__