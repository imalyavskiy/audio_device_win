#ifndef __AUDIO_SYNTH_H__
#define __AUDIO_SYNTH_H__
#pragma once

enum class Waveforms 
{
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_SAWTOOTH,
    WAVE_SINESWEEP,
    WAVE_LAST           // Always keep this entry last
};

enum SYNTH_OUTPUT_FORMAT
{
    SYNTH_OF_PCM,
    SYNTH_OF_MS_ADPCM
};

#define ValidateReadWritePtr(p,cb) 0

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         0x0001
#endif

#ifndef WAVE_FORMAT_ADPCM
#define WAVE_FORMAT_ADPCM       0x0002
#endif

#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE  0xFFFE
#endif


const double TWOPI = 6.283185308;
const int DefaultFrequency = 440;       // A-440
const int MaxAmplitude = 100;
const int MinAmplitude = 0;
const int DefaultSweepStart = DefaultFrequency;
const int DefaultSweepEnd = 5000;

// Class that just reads the raw audio file

struct DataSourceInterface
{
    virtual ~DataSourceInterface() {};

    // Load the buffer with the current waveform
    virtual HRESULT FillPCMAudioBuffer(/*const WAVEFORMATEX& wfex, */BYTE pBuf[], int iBytes) = 0;

    // Set the "current" format and allocate temporary memory
    virtual HRESULT AllocWaveCache(/*const WAVEFORMATEX& wfex*/) = 0;
};

class RawAudioSource
    : public DataSourceInterface
{
protected:
    const uint32_t m_SamplesPerSecond;
    const uint32_t m_BitsPerSample;
    const uint32_t m_Channels;

    std::ifstream  m_source_data;
    std::streamoff m_file_size;
public:
    RawAudioSource(
        std::mutex* pmtx,
        int Frequency = DefaultFrequency,
        Waveforms Waveform = Waveforms::WAVE_SINE,
        int iBitsPerSample = 8,
        int iChannels = 1,
        int iSamplesPerSec = 11025,
        int iAmplitude = 100
    );

    ~RawAudioSource();

    // Load the buffer with the current waveform
    virtual HRESULT FillPCMAudioBuffer(/*const WAVEFORMATEX& wfex, */BYTE pBuf[], int iBytes) override;

    // Set the "current" format and allocate temporary memory
    virtual HRESULT AllocWaveCache(/*const WAVEFORMATEX& wfex*/) override;
};

// Class that synthesizes waveforms
class AudioSynth 
    : public DataSourceInterface
{
public:

    AudioSynth(
        std::mutex* pmtx,
        int Frequency       = DefaultFrequency,
        Waveforms Waveform  = Waveforms::WAVE_SINE,
        int iBitsPerSample  = 8,
        int iChannels       = 1,
        int iSamplesPerSec  = 11025,
        int iAmplitude      = 100
    );

    ~AudioSynth();

    // Load the buffer with the current waveform
    virtual HRESULT FillPCMAudioBuffer(/*const WAVEFORMATEX& wfex, */BYTE pBuf[], int iBytes) override;

    // Set the "current" format and allocate temporary memory
    virtual HRESULT AllocWaveCache(/*const WAVEFORMATEX& wfex*/) override;

    HRESULT get_Frequency(int *Frequency);
    HRESULT put_Frequency(int  Frequency);

    HRESULT get_Waveform(Waveforms &waveForm) const;
    HRESULT put_Waveform(Waveforms waveform);

    HRESULT get_Channels(int *Channels);
    HRESULT put_Channels(int Channels);

    HRESULT get_BitsPerSample(int *BitsPerSample);
    HRESULT put_BitsPerSample(int BitsPerSample);

    HRESULT get_SamplesPerSec(int *SamplesPerSec);
    HRESULT put_SamplesPerSec(int SamplesPerSec);

    HRESULT put_SynthFormat(int Channels, int BitsPerSample, int SamplesPerSec);

    HRESULT get_Amplitude(int *Amplitude);
    HRESULT put_Amplitude(int  Amplitude);

    HRESULT get_SweepRange(int *SweepStart, int *SweepEnd);
    HRESULT put_SweepRange(int  SweepStart, int  SweepEnd);

    HRESULT get_OutputFormat(SYNTH_OUTPUT_FORMAT *pOutputFormat);
    HRESULT put_OutputFormat(SYNTH_OUTPUT_FORMAT ofOutputFormat);

private:
    std::mutex* m_pmtx;

    WORD  m_wChannels;          // The output format's current number of channels.
    WORD  m_wFormatTag;         // The output format.  This can be PCM audio or MS ADPCM audio.
    DWORD m_dwSamplesPerSec;    // The number of samples produced in one second by the synth filter.
    WORD  m_wBitsPerSample;     // The number of bits in each sample.  This member is only valid if the
                                // current format is PCM audio.

    Waveforms m_iWaveform;            // WAVE_SINE ...
    int m_iFrequency;           // if not using sweep, this is the frequency
    int m_iAmplitude;           // 0 to 100

    Waveforms m_iWaveformLast;        // keep track of the last known format
    int m_iFrequencyLast;       // so we can flush the cache if necessary
    int m_iAmplitudeLast;
    int m_iCurrentSample;       // 0 to iSamplesPerSec-1

    BYTE * m_bWaveCache;        // Wave Cache as BYTEs.  This cache ALWAYS holds PCM audio data.
    WORD * m_wWaveCache;        // Wave Cache as WORDs.  This cache ALWAYS holds PCM audio data.

    int m_iWaveCacheSize;       // how big is the cache?
    int m_iWaveCacheCycles;     // how many cycles are in the cache
    int m_iWaveCacheIndex;

    int m_iSweepStart;          // start of sweep
    int m_iSweepEnd;            // end of sweep

    void CalcCache(/*const WAVEFORMATEX& wfex*/);
    void CalcCacheSine(/*const WAVEFORMATEX& wfex*/);
    void CalcCacheSquare(/*const WAVEFORMATEX& wfex*/);
    void CalcCacheSawtooth(/*const WAVEFORMATEX& wfex*/);
    void CalcCacheSweep(/*const WAVEFORMATEX& wfex*/);
};

#endif // __AUDIO_SYNTH_H__
