#include "stdafx.h"
#include "AudioSynth.h"
#include "AudioDeviceBufferInterface.h"
#include "AudioDeviceBuffer.h"

AudioDeviceBuffer::AudioDeviceBuffer()
{
    ;
}

AudioDeviceBuffer::~AudioDeviceBuffer()
{
    ;
}

void
AudioDeviceBuffer::SetVQEData(int play_delay_ms, int rec_delay_ms)
{
    m_play_delay_ms = std::make_unique<int>(play_delay_ms);
    LOG_INFO(L"Play delay == " << *m_play_delay_ms << L" ms");
    
    m_rec_delay_ms = std::make_unique<int>(rec_delay_ms);
    LOG_INFO(L"Rec delay == " << *m_rec_delay_ms << L" ms");
}

int32_t
AudioDeviceBuffer::SetTypingStatus(bool typing_status)
{
    m_typing_status = std::make_unique<bool>(typing_status);
    LOG_INFO(L"Typing status == " << (*m_typing_status ? L"true" : L"false"));
    return 0;
}


int32_t
AudioDeviceBuffer::DeliverRecordedData()
{
    throw std::logic_error("not implemented");

}

int32_t
AudioDeviceBuffer::SetPlayoutPCMFormat(uint32_t samplesPerSecHz, size_t channels, size_t bitsPerSample)
{
    m_playoutSampleRateHz = std::make_unique<uint32_t>(samplesPerSecHz);
    LOG_INFO(L"Playout sample rate == " << *m_playoutSampleRateHz << L" Hz");

    m_playoutChannels = std::make_unique<size_t>(channels);
    LOG_INFO(L"Playout channels == " << *m_playoutChannels);

    m_playoutFrameSizeBytes = std::make_unique<size_t>(channels * bitsPerSample / 8);
    LOG_INFO(L"Playout frame size == " << *m_playoutFrameSizeBytes << L" bytes");

    m_synth.reset(new RawAudioSource/*AudioSynth*/(&m_critical_section
        , 440
        , Waveforms::WAVE_SINE
        , (int)(8 * (*m_playoutFrameSizeBytes) / (*m_playoutChannels))
        , (int)(*m_playoutChannels)
        , *m_playoutSampleRateHz
    ));

    AutoLock l(m_critical_section);
    m_synth->AllocWaveCache();

    return 0;
}

int32_t
AudioDeviceBuffer::PlayoutPCMFormat(uint32_t& samplesPerSecHz, size_t& channels, size_t& bitsPerSample) const
{
    samplesPerSecHz = m_playoutSampleRateHz ? *m_playoutSampleRateHz : 0;

    channels = m_playoutChannels ? *m_playoutChannels : 0;

    bitsPerSample = m_playoutFrameSizeBytes ? 8 * (*m_playoutFrameSizeBytes) / (*m_playoutChannels) : 0;

    return 0;
}

int32_t
AudioDeviceBuffer::SetPlayoutSampleRate(uint32_t fsHz)
{
    m_playoutSampleRateHz = std::make_unique<uint32_t>(fsHz);
    LOG_INFO(L"Playout sample rate == " << *m_playoutSampleRateHz << L" Hz");
    return 0;
}

int32_t  
AudioDeviceBuffer::PlayoutSampleRate() const
{
    return m_playoutSampleRateHz ? *m_playoutSampleRateHz : 0;
}

int32_t
AudioDeviceBuffer::SetPlayoutChannels(size_t channels)
{
    m_playoutChannels = std::make_unique<size_t>(channels);
    LOG_INFO(L"Playout channels == " << *m_playoutChannels);
    return 0;
}

size_t
AudioDeviceBuffer::PlayoutChannels() const
{
    return m_playoutChannels ? *m_playoutChannels : 0;
}

int32_t  
AudioDeviceBuffer::SetPlayoutFrameSize(size_t size)
{
    m_playoutFrameSizeBytes = std::make_unique<size_t>(size);
    LOG_INFO(L"Playout frame size == " << *m_playoutFrameSizeBytes << L" bytes");
    return 0;
}

size_t   
AudioDeviceBuffer::PlayoutFameSize() const
{
    return m_playoutFrameSizeBytes ? *m_playoutFrameSizeBytes : 0;
}

int32_t
AudioDeviceBuffer::RequestPlayoutData(const size_t samples_per_channel)
{
//    const size_t total_samples = (*m_playoutChannels) * samples_per_channel;
    return (int32_t)samples_per_channel;
}

int32_t
AudioDeviceBuffer::GetPlayoutData(void* audio_buffer, uint32_t playBlockSize)
{
    AutoLock l(m_critical_section);
    
    if(SUCCEEDED(m_synth->FillPCMAudioBuffer(static_cast<BYTE*>(audio_buffer), (*m_playoutFrameSizeBytes) * playBlockSize)))
        return ((*m_playoutSampleRateHz) / 100);

    return 0;
}

int32_t
AudioDeviceBuffer::SetRecordingSampleRate(uint32_t fsHz)
{ 
    m_recordingSampleRateHz = std::make_unique<uint32_t>(fsHz);
    LOG_INFO(L"Recording sample rate == " << *m_recordingSampleRateHz << L" Hz");
    return 0;
}

int32_t  
AudioDeviceBuffer::RecordingSampleRate() const
{
    return m_recordingSampleRateHz ? *m_recordingSampleRateHz : 0;
}

int32_t
AudioDeviceBuffer::SetRecordingChannels(size_t channels)
{
    m_recordingChannels = std::make_unique<size_t>(channels);
    LOG_INFO(L"Recording channels == " << *m_recordingChannels);
    return 0;
}

size_t
AudioDeviceBuffer::RecordingChannels() const
{
    return m_recordingChannels ? *m_recordingChannels : 0;
}

int32_t
AudioDeviceBuffer::SetRecordingFrameSize(size_t size)
{
    m_recordingFrameSizeBytes = std::make_unique<size_t>(size);
    LOG_INFO(L"Recording frame size == " << *m_recordingFrameSizeBytes << L" bytes");
    return 0;

}

size_t
AudioDeviceBuffer::RecordingFameSize() const
{
    return m_recordingFrameSizeBytes ? *m_recordingFrameSizeBytes : 0;
}

int32_t
AudioDeviceBuffer::SetRecordedBuffer(const void* audio_buffer, size_t samples_per_channel)
{
    throw std::logic_error("not implemented");
}