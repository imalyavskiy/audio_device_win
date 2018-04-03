#include "stdafx.h"
#include "common.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "AudioSource.h"

const uint32_t riff_4cc = MAKEFOURCC('R', 'I', 'F', 'F');
const uint32_t wave_4cc = MAKEFOURCC('W', 'A', 'V', 'E');
const uint32_t fmt_4cc  = MAKEFOURCC('f', 'm', 't', ' ');
const uint32_t data_4cc = MAKEFOURCC('d', 'a', 't', 'a');

WavAudioSource::WavAudioSource()
{

}

bool WavAudioSource::Init(const std::string& file)
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
    while (m_source_data.rdstate() != std::ios_base::eofbit)
    {
        m_source_data.read(reinterpret_cast<char*>(&riff_descriptor), sizeof(ChunkDescriptor));
        if (m_source_data.rdstate() != std::ios_base::goodbit)
            break;

        const std::streampos riff_payload_begin = m_source_data.tellg();
        const std::streampos riff_payload_end = riff_payload_begin + std::streampos(riff_descriptor.size);

        uint32_t riff_type = 0;
        m_source_data.read(reinterpret_cast<char*>(&riff_type), 4);
        if (m_source_data.rdstate() != std::ios_base::goodbit)
            break;

        if (riff_4cc == riff_descriptor.fourcc || wave_4cc == riff_type)
        {
            if (!ReadWafeRiff(riff_payload_begin, riff_payload_end, m_wave_riff))
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

    m_source_data.clear();
    m_source_data.seekg(0, std::ios_base::beg);

    return (bool)m_wave_riff;
}

bool
WavAudioSource::ReadWafeRiff(const std::streampos& begin, const std::streampos& end, std::unique_ptr<WaveRiff>& wave_riff)
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
        return SUCCEEDED(E_FAIL);

    while (m_source_data.tellg() < end)
    {
        ChunkDescriptor chunk_descriptor{ 0 };
        m_source_data.read(reinterpret_cast<char*>(&chunk_descriptor), sizeof(ChunkDescriptor));
        tmp = m_source_data.tellg();
        if (m_source_data.rdstate() & std::ios_base::failbit)
            return SUCCEEDED(E_FAIL);

        if (chunk_descriptor.fourcc == fmt_4cc)
        {
            if (!ReadFMTChunk(m_source_data.tellg(), chunk_descriptor, riff->format_chunk))
                return false;
        }
        else if (chunk_descriptor.fourcc == data_4cc)
        {
            if (!ReadDataChunk(m_source_data.tellg(), chunk_descriptor, riff->data_chunk))
                return false;
        }
        else
        {
            //m_source_data.seekg(chunk_descriptor.size, std::ios_base::cur); // skip unexpected chunks
            return SUCCEEDED(E_FAIL); // we are expecting only 'fmt ' chunk followed by 'data' chunk
        }
    }

    riff.swap(wave_riff);

    return SUCCEEDED(S_OK);
}

bool
WavAudioSource::ReadFMTChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<FmtChunk>& fmt_chunk)
{
    fmt_chunk.reset();

    if (chunk_descr.size != 16 && chunk_descr.size != 18 && chunk_descr.size != 40)
        return SUCCEEDED(E_INVALIDARG);

    std::unique_ptr<FmtChunk> fmt(new FmtChunk);
    ZeroMemory(fmt.get(), sizeof(FmtChunk));

    if (chunk_descr.size == 16)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk16*>(fmt.get())), sizeof(FmtChunk16));
    else if (chunk_descr.size == 18)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk18*>(fmt.get())), sizeof(FmtChunk18));
    else if (chunk_descr.size == 40)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk40*>(fmt.get())), sizeof(FmtChunk40));

    if ((begin + std::streampos(chunk_descr.size)) != m_source_data.tellg())
        return SUCCEEDED(E_FAIL);

    fmt.swap(fmt_chunk);

    return SUCCEEDED(S_OK);
}

bool
WavAudioSource::ReadDataChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<DataChunk>& data_chunk)
{
    data_chunk.reset();
    std::unique_ptr<DataChunk> chunk(new DataChunk);

    chunk->pos_begin = begin;
    chunk->pos_end = begin + std::streampos(chunk_descr.size);

    m_source_data.seekg(chunk->pos_end);

    chunk.swap(data_chunk);

    return SUCCEEDED(S_OK);
}

WavAudioSource::~WavAudioSource()
{
    ;
}

bool
WavAudioSource::GetFormat(PCMFormat& format)
{
    if (!m_wave_riff->format_chunk || !m_wave_riff->data_chunk)
        return SUCCEEDED(E_FAIL);
    
    PCMFormat::sample_format sample_format = PCMFormat::uns;
    
    switch(m_wave_riff->format_chunk->wBitsPerSample)
    {
    case 8:  sample_format = PCMFormat::ui8; break;
    case 24: sample_format = PCMFormat::i24; break;
    case 16: sample_format = PCMFormat::i16; break;
    case 32: sample_format = PCMFormat::i32; break;
    }

    PCMFormat f
    {
        sample_format,
        m_wave_riff->format_chunk->nSamplesPerSecond,
        m_wave_riff->format_chunk->nChannels,
        m_wave_riff->format_chunk->wBitsPerSample,
        m_wave_riff->format_chunk->nBlockAlign
    };

    memcpy(&format, &f, sizeof(PCMFormat));

    return SUCCEEDED(S_OK);
}

bool
WavAudioSource::ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags)
{
    assert(m_source_data.is_open());
    std::ios_base::iostate rdstate = std::ios_base::goodbit;

    std::streampos file_bytes_rest = m_wave_riff->data_chunk->pos_end - m_wave_riff->data_chunk->pos_begin;

    const uint32_t bufferBytesSize = bufferFrameCount * ((m_wave_riff->format_chunk->nChannels * m_wave_riff->format_chunk->wBitsPerSample) / 8);

    std::streampos curr_pos = m_source_data.tellg();
    if ((curr_pos < m_wave_riff->data_chunk->pos_begin) || (m_wave_riff->data_chunk->pos_end < curr_pos))
        m_source_data.seekg(m_wave_riff->data_chunk->pos_begin, std::ios_base::beg);

    if (bufferBytesSize < file_bytes_rest)
    {
        m_source_data.read(reinterpret_cast<char*>(pData), std::streamsize(bufferBytesSize));

        rdstate = m_source_data.rdstate();
        if (std::ios_base::failbit & rdstate)
            return SUCCEEDED(E_FAIL);

        return SUCCEEDED(S_OK);
    }

    return SUCCEEDED(S_FALSE);
}

bool
WavAudioSource::ReadData(std::shared_ptr<PCMDataBuffer> buffer)
{
    // clean buffer descriptor
    buffer->reset();

    //
    assert(m_source_data.is_open());
    std::ios_base::iostate rdstate = std::ios_base::goodbit;

    //
    assert(0 == (buffer->total_size % m_wave_riff->format_chunk->nBlockAlign));
    if (buffer->total_size % m_wave_riff->format_chunk->nBlockAlign != 0)
        return false;

    //
    std::streampos curr_pos = m_source_data.tellg();
    if ((curr_pos < m_wave_riff->data_chunk->pos_begin) || (m_wave_riff->data_chunk->pos_end < curr_pos))
    {
        m_source_data.seekg(m_wave_riff->data_chunk->pos_begin, std::ios_base::beg);
        curr_pos = m_source_data.tellg();
    }
        
    //
    const std::streamsize bytes_left = m_wave_riff->data_chunk->pos_end - curr_pos;
    const std::streamsize bytes_to_read = bytes_left < buffer->total_size ? bytes_left : buffer->total_size;

    // fill buffer
    if(bytes_to_read > 0)
    {   // read data
        m_source_data.read(reinterpret_cast<char*>(buffer->p.get()), bytes_to_read);

        // check state
        rdstate = m_source_data.rdstate();
        if (((std::ios_base::failbit | std::ios_base::eofbit) & rdstate) > 0)
            return SUCCEEDED(E_FAIL);

        // update buffer descriptor
        buffer->actual_size = m_source_data.gcount();
        assert(buffer->actual_size == bytes_to_read);
            
        // set the buffer is last
        //  - buffer is the last if data left is less then buffer size
        //  - buffer is the last if data just came to an end
        buffer->end_of_stream = bytes_to_read < buffer->total_size || 0 == (bytes_left - bytes_to_read);

        return SUCCEEDED(S_OK);
    }

    return SUCCEEDED(E_FAIL);
}