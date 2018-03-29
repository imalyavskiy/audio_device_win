#ifndef __PCM_STREAM_RENDERER_INTERFACE_H__
#define __PCM_STREAM_RENDERER_INTERFACE_H__
#pragma once

namespace PcmSrtreamRenderer{
    struct Interface
    {
        typedef std::shared_ptr<Interface> ptr;

        enum state
        {
            STATE_NONE,
            STATE_INITIAL,
            STATE_STOPPED,
            STATE_STARTED,
        };
        // TODO(IM):  replace bool with more meaningful type
        virtual bool    SetFormat(const PCMFormat& format, const size_t buffer_frames, const size_t buffers_total) = 0;
        virtual bool    GetFormat(PCMFormat& format, size_t& buffer_frames, size_t& buffers_total) const = 0;

        virtual bool    Start() = 0;
        virtual bool    Stop() = 0;
        virtual bool    WaitForCompletion() = 0;

        virtual state   GetState() const = 0;
        virtual bool    PutBuffer(PCMDataBuffer::wptr& buffer) = 0; // frames == channels * bits_per_sample / 8
        virtual bool    GetBuffer(PCMDataBuffer::wptr& buffer) = 0;

    };

    bool create(const std::string& dump_file, std::shared_ptr<Interface>& instance);
}
#endif // __PCM_STREAM_RENDERER_INTERFACE_H__
