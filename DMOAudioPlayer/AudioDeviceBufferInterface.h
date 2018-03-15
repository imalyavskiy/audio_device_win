#ifndef __AUDIO_DEVICE_BUFFER_H__
#define __AUDIO_DEVICE_BUFFER_H__
#pragma once

namespace webrtc {

    // Delta times between two successive playout callbacks are limited to this
    // value before added to an internal array.
    const size_t kMaxDeltaTimeInMs = 500;
    // TODO(henrika): remove when no longer used by external client.
    const size_t kMaxBufferSizeBytes = 3840;  // 10ms in stereo @ 96kHz

    class AudioDeviceBufferInterface {
    public:
        virtual int32_t  SetRecordingSampleRate(uint32_t fsHz) = 0;
        virtual int32_t  SetPlayoutSampleRate(uint32_t fsHz) = 0;

        virtual int32_t  SetRecordingChannels(size_t channels) = 0;
        virtual int32_t  SetPlayoutChannels(size_t channels) = 0;

        virtual int32_t  SetRecordedBuffer(const void* audio_buffer, size_t samples_per_channel) = 0;
        virtual void     SetVQEData(int play_delay_ms, int rec_delay_ms) = 0;
        virtual int32_t  DeliverRecordedData() = 0;

        virtual int32_t  RequestPlayoutData(size_t samples_per_channel) = 0;
        virtual int32_t  GetPlayoutData(void* audio_buffer) = 0;

        virtual int32_t  SetTypingStatus(bool typing_status) = 0;
    };

}  // namespace webrtc

#endif // __AUDIO_DEVICE_BUFFER_H__
