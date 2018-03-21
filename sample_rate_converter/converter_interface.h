#ifndef __CONVERTER_INTERFACE_H__
#define __CONVERTER_INTERFACE_H__
#pragma once

struct PCMFormat
{
    const uint32_t samplesPerSecond;
    const uint16_t channels;
    const uint32_t bitsPerSample;
};

struct PCMDataBuffer
{
    void* p;
    uint32_t tsize; // total
    uint32_t asize; // actual
};

struct ConverterInterface
{
    virtual ~ConverterInterface() {};

    virtual bool convert(const PCMDataBuffer& in, const PCMDataBuffer& out) = 0;
};

bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p);

#endif // __CONVERTER_INTERFACE_H__
