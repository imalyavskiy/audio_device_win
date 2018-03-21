#ifndef __RENDER_STREAM_H__
#define __RENDER_STREAM_H__
#pragma once

/*
<WAVE-form> → RIFF('WAVE'
                   <fmt-ck>            // Format
                   [<fact-ck>]         // Fact chunk
                   [<cue-ck>]          // Cue points
                   [<playlist-ck>]     // Playlist
                   [<assoc-data-list>] // Associated data list
                   <wave-data> )       // Wave data
*/

struct ChunkDescriptor
{
    uint32_t fourcc;
    uint32_t size;
};

struct FmtChunk16
{
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSecond;
    uint32_t nAvgBytesPerSecond;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSecond;
};

struct DataChunk
{
    std::streampos                          chunk_begin = 0;
    std::streampos                          chunk_end = 0;
};

struct FmtChunk18
    : FmtChunk16
{
    uint16_t cbSize;
};

typedef
struct FmtChunk40
    : FmtChunk18
{
    uint16_t wValidBitsPerSample;
    uint32_t dwChannelMask;
    GUID     SubFormat;
} FmtChunk;

struct CueChunk
{

};

struct PlaylistChunk
{

};

struct AssocDataListChunk
{

};

struct WaveRiff
{
    std::unique_ptr<FmtChunk>               format;
    std::unique_ptr<DataChunk>              data;
};


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

    HRESULT ReadWafeRiff(const std::streampos& begin, const std::streampos& end, std::unique_ptr<WaveRiff>& wave_riff);
    HRESULT ReadFMTChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<FmtChunk>& fmt_chunk);
    HRESULT ReadDataChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<DataChunk>& data_chunk);

protected:
    std::ifstream  m_source_data;
    std::streamoff m_file_size;

    std::unique_ptr<WaveRiff> m_wave_riff;

    WAVEFORMATEX  m_wfx{ WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0 };
//    WAVEFORMATEX  m_wfx{ WAVE_FORMAT_PCM, 2, 48000, 384000, 8, 32, 0 };
};

HRESULT PlayAudioStream(MyAudioSource *pMySource);

#endif // __RENDER_STREAM_H__