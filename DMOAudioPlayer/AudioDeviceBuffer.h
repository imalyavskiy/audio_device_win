#ifndef __AUDIO_BUFFER_H__
#define __AUDIO_BUFFER_H__
#pragma once

class AudioDeviceBuffer
    : public AudioDevicePlayoutBufferInterface
    , public AudioDeviceRecordingBufferInterface
{
public:
    AudioDeviceBuffer();
    ~AudioDeviceBuffer();

    // AudioDeviceBufferInterface
    void     SetVQEData(int play_delay_ms, int rec_delay_ms) override;
    int32_t  SetTypingStatus(bool typing_status) override;

    // AudioDevicePlayoutBufferInterface
    int32_t  SetPlayoutPCMFormat(uint32_t samplesPerSecHz, size_t channels, size_t bitsPerSample) override;
    int32_t  PlayoutPCMFormat(uint32_t& samplesPerSecHz, size_t& channels, size_t& bitsPerSample) const override;

    int32_t  SetPlayoutSampleRate(uint32_t fsHz) override;
    int32_t  PlayoutSampleRate() const override;

    int32_t  SetPlayoutChannels(size_t channels) override;
    size_t   PlayoutChannels() const override;

    int32_t  SetPlayoutFrameSize(size_t size) override;
    size_t   PlayoutFameSize() const override;

    int32_t  RequestPlayoutData(const size_t samples_per_channel) override; // A question of how many samples per sample are ready
                                                                            //  must returns actual number or -1 in case of error

    int32_t  GetPlayoutData(void* audio_buffer, uint32_t playBlockSize) override;

    // AudioDeviceRecordingBufferInterface
    int32_t  SetRecordingSampleRate(uint32_t fsHz) override;
    int32_t  RecordingSampleRate() const override;

    int32_t  SetRecordingChannels(size_t channels) override;
    size_t   RecordingChannels() const override;

    int32_t  SetRecordingFrameSize(size_t size) override;
    size_t   RecordingFameSize() const override;

    int32_t  SetRecordedBuffer(const void* audio_buffer, size_t samples_per_channel) override;

    int32_t  DeliverRecordedData() override;

protected:

    // playout
    std::unique_ptr<uint32_t> m_playoutSampleRateHz;
    std::unique_ptr<size_t>   m_playoutChannels;
    std::unique_ptr<size_t>   m_playoutFrameSizeBytes; // frameSize / playoutChannels * 8 == bits per sample
    
    std::shared_ptr<AudioSynth> m_synth;

    CriticalSection m_critical_section;



    // recording
    std::unique_ptr<uint32_t> m_recordingSampleRateHz;
    std::unique_ptr<size_t>   m_recordingChannels;
    std::unique_ptr<size_t>   m_recordingFrameSizeBytes; // frameSize / playoutChannels * 8 == bits per sample

    // VQEData
    std::unique_ptr<int>      m_play_delay_ms;
    std::unique_ptr<int>      m_rec_delay_ms;

    std::unique_ptr<bool>     m_typing_status;
};
#endif // __AUDIO_BUFFER_H__
