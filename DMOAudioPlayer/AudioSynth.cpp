#include "stdafx.h"
#include "AudioSynth.h"

const DWORD BITS_PER_BYTE = 8;
#ifndef _WIN32_WINNT// as 0x0502
#error
#endif

// inline bool CritCheckIn(CriticalSection * pcCrit)
// {   
//     return (GetCurrentThreadId() == pcCrit->CurrentOwnerId());
// }

//////////////////////////////////////////////////////////////////////////

RawAudioSource::RawAudioSource(std::mutex* pmtx, int Frequency, Waveforms Waveform, int iBitsPerSample, int iChannels, int iSamplesPerSec, int iAmplitude )
    : m_BitsPerSample(iBitsPerSample)
    , m_SamplesPerSecond(iSamplesPerSec)
    , m_Channels(iChannels)
{
    std::streampos begin;
    std::streampos end;

    if (m_SamplesPerSecond == 48000 && m_BitsPerSample == 16 && m_Channels == 2)
    {
        m_source_data.open("C:\\Users\\developer\\etc\\DMOAudioPlayer\\48000_16bit_2ch_LittleEndian.raw", std::ios_base::in | std::ios_base::binary);
        assert(m_source_data.is_open());

        begin = m_source_data.tellg();

        m_source_data.seekg(0, std::ios_base::end);

        end = m_source_data.tellg();

        m_source_data.seekg(0, std::ios_base::beg);

        m_file_size = end - begin;

        assert(0 == m_file_size % ((m_BitsPerSample * m_Channels) / 8));
    }
}

RawAudioSource::~RawAudioSource()
{
    m_source_data.close();
}

// Load the buffer with the current waveform
HRESULT 
RawAudioSource::FillPCMAudioBuffer(/*const WAVEFORMATEX& wfex, */BYTE pBuf[], int iBytes)
{
    std::streampos file_bytes_rest = m_file_size - m_source_data.tellg();
    std::streampos curr_pos;
    if (iBytes < file_bytes_rest)
    {
        m_source_data.read(reinterpret_cast<char*>(pBuf), std::streamsize(iBytes));

        if (std::ios_base::failbit & m_source_data.rdstate())
            return E_FAIL;

        curr_pos = m_source_data.tellg();

        return S_OK;
    }

    return E_FAIL;
}

// Set the "current" format and allocate temporary memory
HRESULT 
RawAudioSource::AllocWaveCache(/*const WAVEFORMATEX& wfex*/)
{
    return S_OK; // here this function does nothing but must exist
}


//////////////////////////////////////////////////////////////////////////

AudioSynth::AudioSynth(std::mutex* pmtx, int Frequency, Waveforms Waveform, int iBitsPerSample, int iChannels, int iSamplesPerSec, int iAmplitude )
    : m_bWaveCache(NULL)
    , m_wWaveCache(NULL)
    , m_pmtx(pmtx)
    , m_iFrequency(Frequency)
    , m_iWaveform(Waveform)
    , m_iAmplitude(iAmplitude)
    , m_iSweepStart(DefaultSweepStart)
    , m_iSweepEnd(DefaultSweepEnd)
    , m_wFormatTag(WAVE_FORMAT_PCM)
    , m_wBitsPerSample((WORD)iBitsPerSample)
    , m_wChannels((WORD)iChannels)
    , m_dwSamplesPerSec(iSamplesPerSec)
{
    assert(Waveform >= Waveforms::WAVE_SINE);
    assert(Waveform < Waveforms::WAVE_LAST);
}

AudioSynth::~AudioSynth()
{
    if (m_bWaveCache)
        delete[] m_bWaveCache;

    if (m_wWaveCache)
        delete[] m_wWaveCache;
}

HRESULT 
AudioSynth::AllocWaveCache(/*const WAVEFORMATEX& wfex*/)
{
    // The caller should hold the state lock because this
    // function uses m_iWaveCacheCycles, m_iWaveCacheSize
    // m_iFrequency, m_bWaveCache and m_wWaveCache.  The
    // function should also hold the state lock because
    // it calls CalcCache().
//    assert(CritCheckIn(m_pStateLock));

    m_iWaveCacheCycles = m_iFrequency;
    m_iWaveCacheSize = (int)m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;

    if (m_bWaveCache)
    {
        delete[] m_bWaveCache;
        m_bWaveCache = NULL;
    }

    if (m_wWaveCache)
    {
        delete[] m_wWaveCache;
        m_wWaveCache = NULL;
    }

    // The wave cache always stores PCM audio data.
    if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8)
    {
        m_bWaveCache = new BYTE[m_iWaveCacheSize];
        if (NULL == m_bWaveCache)
            return E_OUTOFMEMORY;
    }
    else
    {
        m_wWaveCache = new WORD[m_iWaveCacheSize];
        if (NULL == m_wWaveCache)
            return E_OUTOFMEMORY;
    }

    CalcCache(/*wfex*/);

    return S_OK;
}

HRESULT
AudioSynth::FillPCMAudioBuffer(/*const WAVEFORMATEX& wfex, */BYTE pBuf[], int iBytes)
{
    BOOL fCalcCache = FALSE;

    // The caller should always hold the state lock because this
    // function uses m_iFrequency,  m_iFrequencyLast, m_iWaveform
    // m_iWaveformLast, m_iAmplitude, m_iAmplitudeLast, m_iWaveCacheIndex
    // m_iWaveCacheSize, m_bWaveCache and m_wWaveCache.  The caller should
    // also hold the state lock because this function calls CalcCache().
//    assert(CritCheckIn(m_pStateLock));

    // Only realloc the cache if the format has changed !
    if (m_iFrequency != m_iFrequencyLast)
    {
        fCalcCache = TRUE;
        m_iFrequencyLast = m_iFrequency;
    }
    if (m_iWaveform != m_iWaveformLast)
    {
        fCalcCache = TRUE;
        m_iWaveformLast = m_iWaveform;
    }
    if (m_iAmplitude != m_iAmplitudeLast)
    {
        fCalcCache = TRUE;
        m_iAmplitudeLast = m_iAmplitude;
    }

    if (fCalcCache)
    {
        CalcCache(/*wfex*/);
    }

    // Copy cache to output buffers
    if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8 && m_wChannels/*wfex.nChannels*/ == 1)
    {
        while (iBytes--)
        {
            *pBuf++ = m_bWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
    else if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8 && m_wChannels/*wfex.nChannels*/ == 2)
    {
        iBytes /= 2;

        while (iBytes--)
        {
            *pBuf++ = m_bWaveCache[m_iWaveCacheIndex];
            *pBuf++ = m_bWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
    else if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 16 && m_wChannels/*wfex.nChannels*/ == 1)
    {
        WORD * pW = (WORD *)pBuf;
        iBytes /= 2;

        while (iBytes--)
        {
            *pW++ = m_wWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
    else if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 16 && m_wChannels/*wfex.nChannels*/ == 2)
    {
        WORD * pW = (WORD *)pBuf;
        iBytes /= 4;

        while (iBytes--)
        {
            *pW++ = m_wWaveCache[m_iWaveCacheIndex];
            *pW++ = m_wWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }

    return S_OK;
}

void AudioSynth::CalcCache(/*const WAVEFORMATEX& wfex*/)
{
    switch (m_iWaveform)
    {
    case Waveforms::WAVE_SINE:
        CalcCacheSine(/*wfex*/);
        break;

    case Waveforms::WAVE_SQUARE:
        CalcCacheSquare(/*wfex*/);
        break;

    case Waveforms::WAVE_SAWTOOTH:
        CalcCacheSawtooth(/*wfex*/);
        break;

    case Waveforms::WAVE_SINESWEEP:
        CalcCacheSweep(/*wfex*/);
        break;
    }

    m_iWaveformLast = m_iWaveform;
    m_iFrequencyLast = m_iFrequency;
    m_iAmplitudeLast = m_iAmplitude;
}

void AudioSynth::CalcCacheSine(/*const WAVEFORMATEX& wfex*/)
{
    int i;
    double d;
    double amplitude;
    double FTwoPIDivSpS;

    amplitude = ((m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8) ? 127 : 32767) * m_iAmplitude / 100;

    FTwoPIDivSpS = m_iFrequency * TWOPI / m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8)
    {
        BYTE * pB = m_bWaveCache;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            d = FTwoPIDivSpS * i;
            *pB++ = (BYTE)((sin(d) * amplitude) + 128);
        }
    }
    else
    {
        PWORD pW = (PWORD)m_wWaveCache;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            d = FTwoPIDivSpS * i;
            *pW++ = (WORD)(sin(d) * amplitude);
        }
    }
}

void AudioSynth::CalcCacheSquare(/*const WAVEFORMATEX& wfex*/)
{
    int i;
    double d;
    double FTwoPIDivSpS;
    BYTE b0, b1;
    WORD w0, w1;

    b0 = (BYTE)(128 - (127 * m_iAmplitude / 100));
    b1 = (BYTE)(128 + (127 * m_iAmplitude / 100));
    w0 = (WORD)(32767. * m_iAmplitude / 100);
    w1 = (WORD)-(32767. * m_iAmplitude / 100);

    FTwoPIDivSpS = m_iFrequency * TWOPI / m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8)
    {
        BYTE * pB = m_bWaveCache;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            d = FTwoPIDivSpS * i;
            *pB++ = (BYTE)((sin(d) >= 0) ? b1 : b0);
        }
    }
    else
    {
        PWORD pW = (PWORD)m_wWaveCache;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            d = FTwoPIDivSpS * i;
            *pW++ = (WORD)((sin(d) >= 0) ? w1 : w0);
        }
    }
}

void AudioSynth::CalcCacheSawtooth(/*const WAVEFORMATEX& wfex*/)
{
    int i;
    double d;
    double amplitude;
    double FTwoPIDivSpS;
    double step;
    double curstep = 0;
    BOOL fLastWasNeg = TRUE;
    BOOL fPositive;

    amplitude = ((m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8) ? 255 : 65535)
        * m_iAmplitude / 100;

    FTwoPIDivSpS = m_iFrequency * TWOPI / m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;
    step = amplitude * m_iFrequency / m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    BYTE * pB = m_bWaveCache;
    PWORD pW = (PWORD)m_wWaveCache;

    for (i = 0; i < m_iWaveCacheSize; i++)
    {
        d = FTwoPIDivSpS * i;

        // OneShot triggered on positive zero crossing
        fPositive = (sin(d) >= 0);

        if (fLastWasNeg && fPositive)
        {
            if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8)
                curstep = 128 - amplitude / 2;
            else
                curstep = 32768 - amplitude / 2;
        }
        fLastWasNeg = !fPositive;

        if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8)
            *pB++ = (BYTE)curstep;
        else
            *pW++ = (WORD)(-32767 + curstep);

        curstep += step;
    }
}

void AudioSynth::CalcCacheSweep(/*const WAVEFORMATEX& wfex*/)
{
    int i;
    double d;
    double amplitude;
    double FTwoPIDivSpS;
    double CurrentFreq;
    double DeltaFreq;

    amplitude = ((m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8) ? 127 : 32767) * m_iAmplitude / 100;

    DeltaFreq = ((double)m_iSweepEnd - m_iSweepStart) / m_iWaveCacheSize;
    CurrentFreq = m_iSweepStart;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    if (m_wBitsPerSample/*wfex.wBitsPerSample*/ == 8)
    {
        BYTE * pB = m_bWaveCache;
        d = 0.0;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            FTwoPIDivSpS = (int)CurrentFreq * TWOPI / m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;
            CurrentFreq += DeltaFreq;
            d += FTwoPIDivSpS;
            *pB++ = (BYTE)((sin(d) * amplitude) + 128);
        }
    }
    else
    {
        PWORD pW = (PWORD)m_wWaveCache;
        d = 0.0;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            FTwoPIDivSpS = (int)CurrentFreq * TWOPI / m_dwSamplesPerSec/*wfex.nSamplesPerSec*/;
            CurrentFreq += DeltaFreq;
            d += FTwoPIDivSpS;
            *pW++ = (WORD)(sin(d) * amplitude);
        }
    }
}

HRESULT AudioSynth::get_Frequency(int *pFrequency)
{
    if (pFrequency == NULL) 
        return E_POINTER;

    *pFrequency = m_iFrequency;

    LOG_VERBOSE( L"get_Frequency: " << *pFrequency);
    return NOERROR;
}

HRESULT AudioSynth::put_Frequency(int Frequency)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_iFrequency = Frequency;

    LOG_VERBOSE( L"put_Frequency: " << Frequency);
    return NOERROR;
}

HRESULT AudioSynth::get_Waveform(Waveforms &waveForm) const
{
    waveForm = m_iWaveform;

    LOG_VERBOSE( L"get_Waveform: %d" << *waveForm );
    return NOERROR;
}

HRESULT AudioSynth::put_Waveform(Waveforms Waveform)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_iWaveform = Waveform;

    LOG_VERBOSE( L"put_Waveform: " << Waveform);
    return NOERROR;
}

HRESULT AudioSynth::get_Channels(int *pChannels)
{
    if(pChannels == NULL)
        return E_POINTER;

    *pChannels = m_wChannels;

    LOG_VERBOSE( L"get_Channels: %d" << *pChannels);
    return NOERROR;
}

HRESULT AudioSynth::put_Channels(int Channels)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_wChannels = (WORD)Channels;
    return NOERROR;
}

HRESULT AudioSynth::get_BitsPerSample(int *pBitsPerSample)
{
    if(pBitsPerSample == NULL)
        return E_POINTER;

    *pBitsPerSample = m_wBitsPerSample;

    LOG_VERBOSE( L"get_BitsPerSample: %d" << *pBitsPerSample);
    return NOERROR;
}

HRESULT AudioSynth::put_BitsPerSample(int BitsPerSample)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_wBitsPerSample = (WORD)BitsPerSample;
    return NOERROR;
}

HRESULT AudioSynth::get_SamplesPerSec(int *pSamplesPerSec)
{
    if(pSamplesPerSec == NULL)
        return E_POINTER;

    *pSamplesPerSec = m_dwSamplesPerSec;

    LOG_VERBOSE( L"get_SamplesPerSec: %d" << *pSamplesPerSec);
    return NOERROR;
}

HRESULT AudioSynth::put_SamplesPerSec(int SamplesPerSec)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_dwSamplesPerSec = SamplesPerSec;
    return NOERROR;
}

HRESULT AudioSynth::put_SynthFormat(int Channels, int BitsPerSample, int SamplesPerSec)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_wChannels = (WORD)Channels;
    m_wBitsPerSample = (WORD)BitsPerSample;
    m_dwSamplesPerSec = SamplesPerSec;

    LOG_VERBOSE( L"put_SynthFormat: " << BitsPerSample << "-bit " << Channels << "-channel " << SamplesPerSec << "Hz");

    return NOERROR;
}

HRESULT AudioSynth::get_Amplitude(int *pAmplitude)
{
    if(pAmplitude == NULL)
        return E_POINTER;

    *pAmplitude = m_iAmplitude;

    LOG_VERBOSE( L"get_Amplitude: %d" << *pAmplitude);
    return NOERROR;
}

HRESULT AudioSynth::put_Amplitude(int Amplitude)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    if (Amplitude > MaxAmplitude || Amplitude < MinAmplitude)
        return E_INVALIDARG;

    m_iAmplitude = Amplitude;

    LOG_VERBOSE( L"put_Amplitude: %d" << Amplitude);
    return NOERROR;
}

HRESULT AudioSynth::get_SweepRange(int *pSweepStart, int *pSweepEnd)
{
    if(pSweepStart == NULL)
        return E_POINTER;

    if(pSweepEnd == NULL)
        return E_POINTER;

    *pSweepStart = m_iSweepStart;
    *pSweepEnd = m_iSweepEnd;

    LOG_VERBOSE( L"get_SweepStart: " << *pSweepStart << " " << *pSweepEnd);
    return NOERROR;
}

HRESULT AudioSynth::put_SweepRange(int SweepStart, int SweepEnd)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    m_iSweepStart = SweepStart;
    m_iSweepEnd = SweepEnd;

    LOG_VERBOSE( L"put_SweepRange: " << SweepStart << " " << SweepEnd);
    return NOERROR;
}

HRESULT AudioSynth::get_OutputFormat(SYNTH_OUTPUT_FORMAT *pOutputFormat)
{
    if(pOutputFormat == NULL)
        return E_POINTER;

    ValidateReadWritePtr(pOutputFormat, sizeof(SYNTH_OUTPUT_FORMAT));

    switch (m_wFormatTag)
    {
    case WAVE_FORMAT_PCM:
        *pOutputFormat = SYNTH_OF_PCM;
        break;

    case WAVE_FORMAT_ADPCM:
        *pOutputFormat = SYNTH_OF_MS_ADPCM;
        break;

    default:
        return E_UNEXPECTED;
    }

    return S_OK;
}

HRESULT AudioSynth::put_OutputFormat(SYNTH_OUTPUT_FORMAT ofOutputFormat)
{
    std::unique_lock<std::mutex> l(*m_pmtx);

    switch (ofOutputFormat)
    {
    case SYNTH_OF_PCM:
        m_wFormatTag = WAVE_FORMAT_PCM;
        break;

    case SYNTH_OF_MS_ADPCM:
        m_wFormatTag = WAVE_FORMAT_ADPCM;
        break;

    default:
        return E_INVALIDARG;
    }

    return S_OK;
}