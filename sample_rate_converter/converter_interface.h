#ifndef __CONVERTER_INTERFACE_H__
#define __CONVERTER_INTERFACE_H__
#pragma once
struct InputBuffer
{
    void* buffer;
    uint32_t tsize; // total
    uint32_t asize; // actual
};

struct OutputBuffer
{
    void* buffer;
    uint32_t tsize; // total
    uint32_t asize; // actual
};

struct ConverterInterface
{
    virtual ~ConverterInterface() {};

    virtual bool initialize() = 0;

    virtual bool convert() = 0;
};
#endif // __CONVERTER_INTERFACE_H__
