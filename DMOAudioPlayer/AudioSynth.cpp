#include "stdafx.h"
#include "critical_section.h"
#include "AudioSynth.h"

const DWORD BITS_PER_BYTE = 8;

BOOL WINAPI CritCheckIn(CriticalSection * pcCrit)
{
    return (GetCurrentThreadId() == GetThreadId(pcCrit->CurrentOwner()));
}

CAudioSynth::CAudioSynth(CriticalSection* pStateLock,
    int Frequency,
    int Waveform,
    int iBitsPerSample,
    int iChannels,
    int iSamplesPerSec,
    int iAmplitude
)
    : m_bWaveCache(NULL)
    , m_wWaveCache(NULL)
    , m_pStateLock(pStateLock)
{
    assert(Waveform >= WAVE_SINE);
    assert(Waveform < WAVE_LAST);

    m_iFrequency = Frequency;
    m_iWaveform = Waveform;
    m_iAmplitude = iAmplitude;
    m_iSweepStart = DefaultSweepStart;
    m_iSweepEnd = DefaultSweepEnd;

    m_wFormatTag = WAVE_FORMAT_PCM;
    m_wBitsPerSample = (WORD)iBitsPerSample;
    m_wChannels = (WORD)iChannels;
    m_dwSamplesPerSec = iSamplesPerSec;
}

CAudioSynth::~CAudioSynth()
{
    if (m_bWaveCache)
        delete[] m_bWaveCache;

    if (m_wWaveCache)
        delete[] m_wWaveCache;
}

HRESULT CAudioSynth::AllocWaveCache(const WAVEFORMATEX& wfex)
{
    // The caller should hold the state lock because this
    // function uses m_iWaveCacheCycles, m_iWaveCacheSize
    // m_iFrequency, m_bWaveCache and m_wWaveCache.  The
    // function should also hold the state lock because
    // it calls CalcCache().
    assert(CritCheckIn(m_pStateLock));

    m_iWaveCacheCycles = m_iFrequency;
    m_iWaveCacheSize = (int)wfex.nSamplesPerSec;

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
    if (wfex.wBitsPerSample == 8)
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

    CalcCache(wfex);

    return S_OK;
}

void CAudioSynth::GetPCMFormatStructure(WAVEFORMATEX* pwfex)
{
    assert(pwfex);
    if (!pwfex)
        return;

    // The caller must hold the state lock because this function uses
    // m_wChannels, m_wBitsPerSample and m_dwSamplesPerSec.
    assert(CritCheckIn(m_pStateLock));

    // Check for valid input parametes.
    assert((1 == m_wChannels) || (2 == m_wChannels));
    assert((8 == m_wBitsPerSample) || (16 == m_wBitsPerSample));
    assert((8000 == m_dwSamplesPerSec) || (11025 == m_dwSamplesPerSec) ||
        (22050 == m_dwSamplesPerSec) || (44100 == m_dwSamplesPerSec));

    pwfex->wFormatTag = WAVE_FORMAT_PCM;
    pwfex->nChannels = m_wChannels;
    pwfex->nSamplesPerSec = m_dwSamplesPerSec;
    pwfex->wBitsPerSample = m_wBitsPerSample;
    pwfex->nBlockAlign = (WORD)((pwfex->wBitsPerSample * pwfex->nChannels) / BITS_PER_BYTE);
    pwfex->nAvgBytesPerSec = pwfex->nBlockAlign * pwfex->nSamplesPerSec;
    pwfex->cbSize = 0;
}

void CAudioSynth::FillPCMAudioBuffer(const WAVEFORMATEX& wfex, BYTE pBuf[], int iSize)
{
    BOOL fCalcCache = FALSE;

    // The caller should always hold the state lock because this
    // function uses m_iFrequency,  m_iFrequencyLast, m_iWaveform
    // m_iWaveformLast, m_iAmplitude, m_iAmplitudeLast, m_iWaveCacheIndex
    // m_iWaveCacheSize, m_bWaveCache and m_wWaveCache.  The caller should
    // also hold the state lock because this function calls CalcCache().
    assert(CritCheckIn(m_pStateLock));

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
        CalcCache(wfex);
    }

    // Copy cache to output buffers
    if (wfex.wBitsPerSample == 8 && wfex.nChannels == 1)
    {
        while (iSize--)
        {
            *pBuf++ = m_bWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
    else if (wfex.wBitsPerSample == 8 && wfex.nChannels == 2)
    {
        iSize /= 2;

        while (iSize--)
        {
            *pBuf++ = m_bWaveCache[m_iWaveCacheIndex];
            *pBuf++ = m_bWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
    else if (wfex.wBitsPerSample == 16 && wfex.nChannels == 1)
    {
        WORD * pW = (WORD *)pBuf;
        iSize /= 2;

        while (iSize--)
        {
            *pW++ = m_wWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
    else if (wfex.wBitsPerSample == 16 && wfex.nChannels == 2)
    {
        WORD * pW = (WORD *)pBuf;
        iSize /= 4;

        while (iSize--)
        {
            *pW++ = m_wWaveCache[m_iWaveCacheIndex];
            *pW++ = m_wWaveCache[m_iWaveCacheIndex++];
            if (m_iWaveCacheIndex >= m_iWaveCacheSize)
                m_iWaveCacheIndex = 0;
        }
    }
}

void CAudioSynth::CalcCache(const WAVEFORMATEX& wfex)
{
    switch (m_iWaveform)
    {
    case WAVE_SINE:
        CalcCacheSine(wfex);
        break;

    case WAVE_SQUARE:
        CalcCacheSquare(wfex);
        break;

    case WAVE_SAWTOOTH:
        CalcCacheSawtooth(wfex);
        break;

    case WAVE_SINESWEEP:
        CalcCacheSweep(wfex);
        break;
    }
}

void CAudioSynth::CalcCacheSine(const WAVEFORMATEX& wfex)
{
    int i;
    double d;
    double amplitude;
    double FTwoPIDivSpS;

    amplitude = ((wfex.wBitsPerSample == 8) ? 127 : 32767) * m_iAmplitude / 100;

    FTwoPIDivSpS = m_iFrequency * TWOPI / wfex.nSamplesPerSec;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    if (wfex.wBitsPerSample == 8)
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

void CAudioSynth::CalcCacheSquare(const WAVEFORMATEX& wfex)
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

    FTwoPIDivSpS = m_iFrequency * TWOPI / wfex.nSamplesPerSec;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    if (wfex.wBitsPerSample == 8)
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

void CAudioSynth::CalcCacheSawtooth(const WAVEFORMATEX& wfex)
{
    int i;
    double d;
    double amplitude;
    double FTwoPIDivSpS;
    double step;
    double curstep = 0;
    BOOL fLastWasNeg = TRUE;
    BOOL fPositive;

    amplitude = ((wfex.wBitsPerSample == 8) ? 255 : 65535)
        * m_iAmplitude / 100;

    FTwoPIDivSpS = m_iFrequency * TWOPI / wfex.nSamplesPerSec;
    step = amplitude * m_iFrequency / wfex.nSamplesPerSec;

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
            if (wfex.wBitsPerSample == 8)
                curstep = 128 - amplitude / 2;
            else
                curstep = 32768 - amplitude / 2;
        }
        fLastWasNeg = !fPositive;

        if (wfex.wBitsPerSample == 8)
            *pB++ = (BYTE)curstep;
        else
            *pW++ = (WORD)(-32767 + curstep);

        curstep += step;
    }
}

void CAudioSynth::CalcCacheSweep(const WAVEFORMATEX& wfex)
{
    int i;
    double d;
    double amplitude;
    double FTwoPIDivSpS;
    double CurrentFreq;
    double DeltaFreq;

    amplitude = ((wfex.wBitsPerSample == 8) ? 127 : 32767) * m_iAmplitude / 100;

    DeltaFreq = ((double)m_iSweepEnd - m_iSweepStart) / m_iWaveCacheSize;
    CurrentFreq = m_iSweepStart;

    m_iWaveCacheIndex = 0;
    m_iCurrentSample = 0;

    if (wfex.wBitsPerSample == 8)
    {
        BYTE * pB = m_bWaveCache;
        d = 0.0;

        for (i = 0; i < m_iWaveCacheSize; i++)
        {
            FTwoPIDivSpS = (int)CurrentFreq * TWOPI / wfex.nSamplesPerSec;
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
            FTwoPIDivSpS = (int)CurrentFreq * TWOPI / wfex.nSamplesPerSec;
            CurrentFreq += DeltaFreq;
            d += FTwoPIDivSpS;
            *pW++ = (WORD)(sin(d) * amplitude);
        }
    }
}

HRESULT CAudioSynth::get_Frequency(int *pFrequency)
{
    if (pFrequency == NULL) 
        return E_POINTER;

    *pFrequency = m_iFrequency;

    RTC_LOG(LS_VERBOSE << L"get_Frequency: " << *pFrequency);
    return NOERROR;
}

HRESULT CAudioSynth::put_Frequency(int Frequency)
{
    AutoLock l(m_pStateLock);

    m_iFrequency = Frequency;

    RTC_LOG(LS_VERBOSE << L"put_Frequency: " << Frequency);
    return NOERROR;
}

HRESULT CAudioSynth::get_Waveform(int *pWaveform)
{
    if(pWaveform == NULL)
        return E_POINTER;
    *pWaveform = m_iWaveform;

    RTC_LOG(LS_VERBOSE << L"get_Waveform: %d" << *pWaveform );
    return NOERROR;
}

HRESULT CAudioSynth::put_Waveform(int Waveform)
{
    AutoLock l(m_pStateLock);

    m_iWaveform = Waveform;

    RTC_LOG(LS_VERBOSE << L"put_Waveform: " << Waveform);
    return NOERROR;
}

HRESULT CAudioSynth::get_Channels(int *pChannels)
{
    if(pChannels == NULL)
        return E_POINTER;

    *pChannels = m_wChannels;

    RTC_LOG(LS_VERBOSE << L"get_Channels: %d" << *pChannels);
    return NOERROR;
}

HRESULT CAudioSynth::put_Channels(int Channels)
{
    AutoLock l(m_pStateLock);

    m_wChannels = (WORD)Channels;
    return NOERROR;
}

HRESULT CAudioSynth::get_BitsPerSample(int *pBitsPerSample)
{
    if(pBitsPerSample == NULL)
        return E_POINTER;

    *pBitsPerSample = m_wBitsPerSample;

    RTC_LOG(LS_VERBOSE << L"get_BitsPerSample: %d" << *pBitsPerSample);
    return NOERROR;
}

HRESULT CAudioSynth::put_BitsPerSample(int BitsPerSample)
{
    AutoLock l(m_pStateLock);

    m_wBitsPerSample = (WORD)BitsPerSample;
    return NOERROR;
}

HRESULT CAudioSynth::get_SamplesPerSec(int *pSamplesPerSec)
{
    if(pSamplesPerSec == NULL)
        return E_POINTER;

    *pSamplesPerSec = m_dwSamplesPerSec;

    RTC_LOG(LS_VERBOSE << L"get_SamplesPerSec: %d" << *pSamplesPerSec);
    return NOERROR;
}

HRESULT CAudioSynth::put_SamplesPerSec(int SamplesPerSec)
{
    AutoLock l(m_pStateLock);

    m_dwSamplesPerSec = SamplesPerSec;
    return NOERROR;
}

HRESULT CAudioSynth::put_SynthFormat(int Channels, int BitsPerSample, int SamplesPerSec)
{
    AutoLock l(m_pStateLock);

    m_wChannels = (WORD)Channels;
    m_wBitsPerSample = (WORD)BitsPerSample;
    m_dwSamplesPerSec = SamplesPerSec;

    RTC_LOG(LS_VERBOSE << L"put_SynthFormat: " << BitsPerSample << "-bit " << Channels << "-channel " << SamplesPerSec << "Hz");

    return NOERROR;
}

HRESULT CAudioSynth::get_Amplitude(int *pAmplitude)
{
    if(pAmplitude == NULL)
        return E_POINTER;

    *pAmplitude = m_iAmplitude;

    RTC_LOG(LS_VERBOSE << L"get_Amplitude: %d" << *pAmplitude);
    return NOERROR;
}

HRESULT CAudioSynth::put_Amplitude(int Amplitude)
{
    AutoLock l(m_pStateLock);

    if (Amplitude > MaxAmplitude || Amplitude < MinAmplitude)
        return E_INVALIDARG;

    m_iAmplitude = Amplitude;

    RTC_LOG(LS_VERBOSE << L"put_Amplitude: %d" << Amplitude);
    return NOERROR;
}

HRESULT CAudioSynth::get_SweepRange(int *pSweepStart, int *pSweepEnd)
{
    if(pSweepStart == NULL)
        return E_POINTER;

    if(pSweepEnd == NULL)
        return E_POINTER;

    *pSweepStart = m_iSweepStart;
    *pSweepEnd = m_iSweepEnd;

    RTC_LOG(LS_VERBOSE << L"get_SweepStart: " << *pSweepStart << " " << *pSweepEnd);
    return NOERROR;
}

HRESULT CAudioSynth::put_SweepRange(int SweepStart, int SweepEnd)
{
    AutoLock l(m_pStateLock);

    m_iSweepStart = SweepStart;
    m_iSweepEnd = SweepEnd;

    RTC_LOG(LS_VERBOSE << L"put_SweepRange: " << SweepStart << " " << SweepEnd);
    return NOERROR;
}

HRESULT CAudioSynth::get_OutputFormat(SYNTH_OUTPUT_FORMAT *pOutputFormat)
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

HRESULT CAudioSynth::put_OutputFormat(SYNTH_OUTPUT_FORMAT ofOutputFormat)
{
    AutoLock l(m_pStateLock);

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