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
    
    void* p;
    uint32_t tsize;         // total
    uint32_t asize;         // actual
};

struct ConverterInterface
{
    typedef std::shared_ptr<ConverterInterface> ptr;

    virtual ~ConverterInterface() {};

    virtual bool convert(PCMDataBuffer& in, PCMDataBuffer& out, bool no_more_data) = 0;
};

bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p);

#endif // __CONVERTER_INTERFACE_H__
