#ifndef __CONVERTER_INTERFACE_H__
#define __CONVERTER_INTERFACE_H__
#pragma once

struct PCMFormat
{
    const uint32_t samplesPerSecond;
    const uint16_t channels;
    const uint32_t bitsPerSample;
    const uint32_t bytesPerFrame;
};

struct PCMDataBuffer
{
    typedef std::shared_ptr<PCMDataBuffer> sptr;
    typedef std::weak_ptr<PCMDataBuffer> wptr;
    
    inline void reset() { actual_size = 0; end_of_stream = 0; };

    // buffer
    void * const p; // const pointer to modifiable data

    // total
    const std::streamsize total_size;

    // actual
    std::streamsize actual_size;

    // is it the last buffer in the sequence
    bool     end_of_stream;
};

struct ConverterInterface
{
    typedef std::shared_ptr<ConverterInterface> ptr;

    virtual ~ConverterInterface() {};

    virtual bool convert(PCMDataBuffer& in, PCMDataBuffer& out, bool no_more_data) = 0;
};

bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p);

#endif // __CONVERTER_INTERFACE_H__
