#ifndef __CONVERTER_H__
#define __CONVERTER_H__
#pragma once

class Converter
    : public ConverterInterface
{
public:
    Converter();
    ~Converter();

    // ConverterInterface
    bool initialize(const uint32_t samplesPerSecond, const uint16_t channels, const uint32_t bitsPerSample) override;
    bool convert() override;
protected:
    uint32_t m_samplesPerSecond = uint32_t(-1);
    uint16_t m_channels         = uint16_t(-1);
    uint32_t m_bitsPerSample    = uint32_t(-1);

    SRC_STATE * m_converter_inst;
};

#endif //__CONVERTER_H__