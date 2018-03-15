#include "stdafx.h"
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

int32_t
AudioDeviceBuffer::SetRecordingSampleRate(uint32_t fsHz)
{ 
    m_recordingSampleRateHz = std::make_unique<uint32_t>(fsHz);
    return 0;
}

int32_t
AudioDeviceBuffer::SetPlayoutSampleRate(uint32_t fsHz)
{
    m_playoutSampleRateHz = std::make_unique<uint32_t>(fsHz);
    return 0;
}

int32_t
AudioDeviceBuffer::SetRecordingChannels(size_t channels)
{
    m_recordingChannels = std::make_unique<size_t>(channels);
    return 0;
}

int32_t
AudioDeviceBuffer::SetPlayoutChannels(size_t channels)
{
    m_playoutChannels = std::make_unique<size_t>(channels);
    return 0;
}

int32_t
AudioDeviceBuffer::SetRecordedBuffer(const void* audio_buffer, size_t samples_per_channel)
{
    throw std::logic_error("not implemented");
}

void
AudioDeviceBuffer::SetVQEData(int play_delay_ms, int rec_delay_ms)
{
    m_play_delay_ms = std::make_unique<int>(play_delay_ms);
    m_rec_delay_ms = std::make_unique<int>(rec_delay_ms);
}

int32_t
AudioDeviceBuffer::DeliverRecordedData()
{
    throw std::logic_error("not implemented");
}

int32_t
AudioDeviceBuffer::RequestPlayoutData(size_t samples_per_channel)
{
    throw std::logic_error("not implemented");
}

int32_t
AudioDeviceBuffer::GetPlayoutData(void* audio_buffer)
{
    throw std::logic_error("not implemented");
}

int32_t
AudioDeviceBuffer::SetTypingStatus(bool typing_status)
{
    m_typing_status = std::make_unique<bool>(typing_status);
    return 0;
}