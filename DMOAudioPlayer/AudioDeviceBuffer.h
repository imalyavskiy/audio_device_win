#ifndef __AUDIO_BUFFER_H__
#define __AUDIO_BUFFER_H__
#pragma once

class AudioDeviceBuffer
    : public AudioDeviceBufferInterface
{
public:
    AudioDeviceBuffer();
    ~AudioDeviceBuffer();

    int32_t  SetRecordingSampleRate(uint32_t fsHz) override;

    int32_t  SetPlayoutSampleRate(uint32_t fsHz) override;

    int32_t  SetRecordingChannels(size_t channels) override;

    int32_t  SetPlayoutChannels(size_t channels) override;

    int32_t  SetRecordedBuffer(const void* audio_buffer, size_t samples_per_channel) override;

    void     SetVQEData(int play_delay_ms, int rec_delay_ms) override;

    int32_t  DeliverRecordedData() override;

    int32_t  RequestPlayoutData(size_t samples_per_channel) override;

    int32_t  GetPlayoutData(void* audio_buffer) override;

    int32_t  SetTypingStatus(bool typing_status) override;

protected:

    // recording
    std::unique_ptr<uint32_t> m_recordingSampleRateHz;
    std::unique_ptr<size_t>   m_recordingChannels;

    // playout
    std::unique_ptr<uint32_t> m_playoutSampleRateHz;
    std::unique_ptr<size_t>   m_playoutChannels;
    
    // VQEData
    std::unique_ptr<int>      m_play_delay_ms;
    std::unique_ptr<int>      m_rec_delay_ms;

    std::unique_ptr<bool>     m_typing_status;
};
#endif // __AUDIO_BUFFER_H__
