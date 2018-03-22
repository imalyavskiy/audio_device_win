#include "stdafx.h"
#include "AudioSourceInterface.h"
#include "AudioSource.h"

const uint32_t riff_4cc = MAKEFOURCC('R', 'I', 'F', 'F');
const uint32_t wave_4cc = MAKEFOURCC('W', 'A', 'V', 'E');
const uint32_t fmt_4cc = MAKEFOURCC('f', 'm', 't', ' ');
const uint32_t data_4cc = MAKEFOURCC('d', 'a', 't', 'a');

std::shared_ptr<AudioSource>
AudioSource::Create(const std::string& file)
{
    AudioSource* p = new AudioSource();

    if (!p->Init(file))
        return {};

    return std::shared_ptr<AudioSource>(p);
}

AudioSource::AudioSource()
{

}

bool AudioSource::Init(const std::string& file)
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
            if (FAILED(ReadWafeRiff(riff_payload_begin, riff_payload_end, m_wave_riff)))
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

HRESULT
AudioSource::ReadWafeRiff(const std::streampos& begin, const std::streampos& end, std::unique_ptr<WaveRiff>& wave_riff)
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
        return E_FAIL;

    while (m_source_data.tellg() < end)
    {
        ChunkDescriptor chunk_descriptor{ 0 };
        m_source_data.read(reinterpret_cast<char*>(&chunk_descriptor), sizeof(ChunkDescriptor));
        tmp = m_source_data.tellg();
        if (m_source_data.rdstate() & std::ios_base::failbit)
            return E_FAIL;

        if (chunk_descriptor.fourcc == fmt_4cc)
        {
            hr = ReadFMTChunk(m_source_data.tellg(), chunk_descriptor, riff->format_chunk);
            if (FAILED(hr))
                return hr;
        }
        else if (chunk_descriptor.fourcc == data_4cc)
        {
            hr = ReadDataChunk(m_source_data.tellg(), chunk_descriptor, riff->data_chunk);
            if (FAILED(hr))
                return hr;
        }
        else
        {
            //m_source_data.seekg(chunk_descriptor.size, std::ios_base::cur); // skip unexpected chunks
            return E_FAIL; // we are expecting only 'fmt ' chunk followed by 'data' chunk
        }
    }

    riff.swap(wave_riff);

    return S_OK;
}

HRESULT
AudioSource::ReadFMTChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<FmtChunk>& fmt_chunk)
{
    fmt_chunk.reset();

    if (chunk_descr.size != 16 && chunk_descr.size != 18 && chunk_descr.size != 40)
        return E_INVALIDARG;

    std::unique_ptr<FmtChunk> fmt(new FmtChunk);
    ZeroMemory(fmt.get(), sizeof(FmtChunk));

    if (chunk_descr.size == 16)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk16*>(fmt.get())), sizeof(FmtChunk16));
    else if (chunk_descr.size == 18)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk18*>(fmt.get())), sizeof(FmtChunk18));
    else if (chunk_descr.size == 40)
        m_source_data.read(reinterpret_cast<char*>(static_cast<FmtChunk40*>(fmt.get())), sizeof(FmtChunk40));

    if ((begin + std::streampos(chunk_descr.size)) != m_source_data.tellg())
        return E_FAIL;

    fmt.swap(fmt_chunk);

    return S_OK;
}

HRESULT
AudioSource::ReadDataChunk(const std::streampos& begin, const ChunkDescriptor& chunk_descr, std::unique_ptr<DataChunk>& data_chunk)
{
    data_chunk.reset();
    std::unique_ptr<DataChunk> chunk(new DataChunk);

    chunk->pos_begin = begin;
    chunk->pos_end = begin + std::streampos(chunk_descr.size);

    m_source_data.seekg(chunk->pos_end);

    chunk.swap(data_chunk);

    return S_OK;
}

AudioSource::~AudioSource()
{
    ;
}

HRESULT
AudioSource::GetFormat(std::unique_ptr<WAVEFORMATEXTENSIBLE>& pwfx)
{
    pwfx.reset();

    if (!m_wave_riff->format_chunk || !m_wave_riff->data_chunk)
        return E_FAIL;

    pwfx.release();
    pwfx = std::unique_ptr<WAVEFORMATEXTENSIBLE>(new WAVEFORMATEXTENSIBLE);
    ZeroMemory(pwfx.get(), sizeof(WAVEFORMATEXTENSIBLE));

    pwfx->Format.wFormatTag = 0xfffe;
    pwfx->Format.nChannels = m_wave_riff->format_chunk->nChannels;
    pwfx->Format.nSamplesPerSec = m_wave_riff->format_chunk->nSamplesPerSecond;
    pwfx->Format.nAvgBytesPerSec = m_wave_riff->format_chunk->nAvgBytesPerSecond;
    pwfx->Format.nBlockAlign = m_wave_riff->format_chunk->nBlockAlign;
    pwfx->Format.wBitsPerSample = m_wave_riff->format_chunk->wBitsPerSample;
    pwfx->Format.cbSize = 22;
    pwfx->Samples.wValidBitsPerSample = pwfx->Format.wBitsPerSample;
    pwfx->dwChannelMask = 0x3;
    pwfx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    return S_OK;
}

HRESULT
AudioSource::ReadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* pFlags)
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
            return E_FAIL;

        return S_OK;
    }

    return S_FALSE;
}