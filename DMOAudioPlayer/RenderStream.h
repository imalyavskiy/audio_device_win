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
    uint16_t wBitsPerSample;
};

struct DataChunk
{
    std::streampos                          pos_begin = 0;
    std::streampos                          pos_end = 0;
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
    std::unique_ptr<FmtChunk>               format_chunk;
    std::unique_ptr<DataChunk>              data_chunk;
};


struct MyAudioSource
{
    virtual ~MyAudioSource() {};

    virtual HRESULT GetFormat(std::unique_ptr<WAVEFORMATEXTENSIBLE>& pwfx) = 0;
    virtual HRESULT ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) = 0;

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

    virtual HRESULT GetFormat(std::unique_ptr<WAVEFORMATEXTENSIBLE>& pwfx) override;
    virtual HRESULT ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) override;

    HRESULT ReadWafeRiff(const std::streampos& begin, const std::streampos& end, std::unique_ptr<WaveRiff>& wave_riff);
    HRESULT ReadFMTChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<FmtChunk>& fmt_chunk);
    HRESULT ReadDataChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<DataChunk>& data_chunk);

protected:
    std::ifstream  m_source_data;
    std::streamoff m_file_size;

    std::unique_ptr<WaveRiff> m_wave_riff;
};

HRESULT PlayAudioStream(MyAudioSource *pMySource);

#endif // __RENDER_STREAM_H__