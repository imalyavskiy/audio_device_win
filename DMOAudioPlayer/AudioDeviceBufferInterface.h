#ifndef __AUDIO_DEVICE_BUFFER_H__
#define __AUDIO_DEVICE_BUFFER_H__
#pragma once

// Delta times between two successive playout callbacks are limited to this
// value before added to an internal array.
const size_t kMaxDeltaTimeInMs = 500;
// TODO(henrika): remove when no longer used by external client.
const size_t kMaxBufferSizeBytes = 3840;  // 10ms in stereo @ 96kHz

struct AudioDeviceBufferInterface {

    virtual ~AudioDeviceBufferInterface() {};

    virtual void     SetVQEData(int play_delay_ms, int rec_delay_ms) = 0;
    virtual int32_t  SetTypingStatus(bool typing_status) = 0;
};

struct AudioDevicePlayoutBufferInterface : AudioDeviceBufferInterface
{
    typedef std::shared_ptr<AudioDevicePlayoutBufferInterface> ptr;

    virtual int32_t  SetPlayoutPCMFormat(uint32_t samplesPerSecHz, size_t channels, size_t bitsPerSample) = 0;
    virtual int32_t  PlayoutPCMFormat(uint32_t& samplesPerSecHz, size_t& channels, size_t& bitsPerSample) const = 0;

    virtual int32_t  SetPlayoutSampleRate(uint32_t fsHz) = 0;
    virtual int32_t  PlayoutSampleRate() const = 0;

    virtual int32_t  SetPlayoutChannels(size_t channels) = 0;
    virtual size_t   PlayoutChannels() const = 0;

    virtual int32_t  SetPlayoutFrameSize(size_t size) = 0;
    virtual size_t   PlayoutFameSize() const = 0;

    virtual int32_t  RequestPlayoutData(const size_t samples_per_channel) = 0;
    virtual int32_t  GetPlayoutData(void* audio_buffer) = 0;
};

struct AudioDeviceRecordingBufferInterface : AudioDeviceBufferInterface
{
    typedef std::shared_ptr<AudioDeviceRecordingBufferInterface> ptr;

    virtual int32_t  SetRecordingSampleRate(uint32_t fsHz) = 0;
    virtual int32_t  RecordingSampleRate() const = 0;

    virtual size_t   RecordingChannels() const = 0;
    virtual int32_t  SetRecordingChannels(size_t channels) = 0;

    virtual int32_t  SetRecordingFrameSize(size_t size) = 0;
    virtual size_t   RecordingFameSize() const = 0;

    virtual int32_t  SetRecordedBuffer(const void* audio_buffer, size_t samples_per_channel) = 0;
    virtual int32_t  DeliverRecordedData() = 0;
};

#endif // __AUDIO_DEVICE_BUFFER_H__
