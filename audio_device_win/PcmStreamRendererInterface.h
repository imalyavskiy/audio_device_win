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

    virtual bool    GetFormat(PCMFormat& format) const = 0;
    virtual bool    SetDataPort(common::DataPortInterface::wptr converter_out) = 0;
    virtual bool    Start() = 0;
    virtual bool    Stop() = 0;
    virtual bool    WaitForCompletion() = 0;
};

bool create(const std::string& dump_file, std::shared_ptr<IPcmSrtreamRenderer>& instance);

#endif // __PCM_STREAM_RENDERER_INTERFACE_H__
