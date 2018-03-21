#ifndef __CONVERTER_H__
#define __CONVERTER_H__
#pragma once

class Converter
    : public ConverterInterface
{
    friend bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p);

public:
    ~Converter();
    Converter(const PCMFormat& format_in, const PCMFormat& format_out);

protected:
    bool initialize();

    // ConverterInterface
    bool convert(const PCMDataBuffer& buffer_in, const PCMDataBuffer& buffer_out) override;

    const PCMFormat m_format_in;
    const PCMFormat m_format_out;

    SRC_STATE * m_converter_inst;
};

#endif //__CONVERTER_H__