#ifndef __AUDIO_SOURCE_H__
#define __AUDIO_SOURCE_H__
#pragma once

/*
<WAVE-form> -> RIFF('WAVE'
<fmt-ck>            // Format
[<fact-ck>]         // Fact chunk
[<cue-ck>]          // Cue points
[<playlist-ck>]     // Playlist
[<assoc-data-list>] // Associated data list
<wave-data> )       // Wave data
*/

namespace WavAudioSource
{
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
        std::streampos pos_begin = 0;
        std::streampos pos_end = 0;
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

    class Implementation
        : public Interface
    {
        friend bool WavAudioSource::create(const std::string& file, std::shared_ptr<Interface>& source);

    public:
        ~Implementation();

    protected:
        Implementation();

        bool Init(const std::string& file);

        virtual bool GetFormat(PCMFormat& format) override;
        virtual bool ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags) override;
        virtual bool ReadData(std::shared_ptr<PCMDataBuffer> buffer) override;

        bool ReadWafeRiff(const std::streampos& begin, const std::streampos& end, std::unique_ptr<WaveRiff>& wave_riff);
        bool ReadFMTChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<FmtChunk>& fmt_chunk);
        bool ReadDataChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<DataChunk>& data_chunk);

    protected:
        std::ifstream  m_source_data;
        std::streamoff m_file_size;
        std::streampos m_data_chunk_bytes_total = 0;
        std::streampos m_data_chunk_bytes_rest  = 0;

        std::unique_ptr<WaveRiff> m_wave_riff;
    };
}

#endif // __AUDIO_SOURCE_H__

