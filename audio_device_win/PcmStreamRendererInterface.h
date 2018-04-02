#ifndef __PCM_STREAM_RENDERER_INTERFACE_H__
#define __PCM_STREAM_RENDERER_INTERFACE_H__
#pragma once

struct IPcmSrtreamRenderer
{
    typedef std::shared_ptr<IPcmSrtreamRenderer> ptr;

    enum state
    {
        STATE_NONE,
        STATE_INITIAL,
        STATE_STOPPED,
        STATE_STARTED,
    };

    // TODO(IM):  replace bool with more meaningful type
    virtual bool    SetFormat(const PCMFormat& format) = 0;
    virtual bool    GetFormat(PCMFormat& format) const = 0;

    virtual bool    Start() = 0;
    virtual bool    Stop() = 0;
    virtual bool    WaitForCompletion() = 0;

    virtual state   GetState() const = 0;
    virtual bool    PutBuffer(PCMDataBuffer::wptr& buffer) = 0; // frames == channels * bits_per_sample / 8
    virtual bool    GetBuffer(PCMDataBuffer::wptr& buffer) = 0;

};

bool create(const std::string& dump_file, std::shared_ptr<IPcmSrtreamRenderer>& instance);

#endif // __PCM_STREAM_RENDERER_INTERFACE_H__
