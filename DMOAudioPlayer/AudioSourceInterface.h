#ifndef __AUDIO_SOURCE_INTERFACE_H__
#define __AUDIO_SOURCE_INTERFACE_H__
#pragma once

struct AudioSourceInterface
{
    virtual ~AudioSourceInterface() {};

    virtual HRESULT GetFormat(std::unique_ptr<WAVEFORMATEXTENSIBLE>& pwfx) = 0;
    virtual HRESULT ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) = 0;

};

#endif // __AUDIO_SOURCE_INTERFACE_H__