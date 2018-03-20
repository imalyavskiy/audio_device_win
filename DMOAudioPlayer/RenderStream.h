#ifndef __RENDER_STREAM_H__
#define __RENDER_STREAM_H__
#pragma once

struct MyAudioSource
{
    virtual ~MyAudioSource() {};

    virtual HRESULT GetFormat(std::unique_ptr<WAVEFORMATEX>& pwfx) = 0;
    virtual HRESULT LoadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) = 0;
};

class MyAudioSourceImpl
    : public MyAudioSource
{
public:
    static std::shared_ptr<MyAudioSource> Create(const std::string& file);

    ~MyAudioSourceImpl();

protected:
    MyAudioSourceImpl();
    
    bool Init(const std::string& file);

    virtual HRESULT GetFormat(std::unique_ptr<WAVEFORMATEX>& pwfx) override;
    virtual HRESULT LoadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) override;

protected:
    std::ifstream  m_source_data;
    std::streamoff m_file_size;

    WAVEFORMATEX  m_wfx{ WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0 };
//    WAVEFORMATEX  m_wfx{ WAVE_FORMAT_PCM, 2, 48000, 384000, 8, 32, 0 };
};

HRESULT PlayAudioStream(MyAudioSource *pMySource);

#endif // __RENDER_STREAM_H__