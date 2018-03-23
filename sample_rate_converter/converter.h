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
    bool convert(PCMDataBuffer& buffer_in, PCMDataBuffer& buffer_out, bool no_more_data) override;

    // utility
    void update_proxy_buffers(const PCMDataBuffer& buffer_in, const PCMDataBuffer& buffer_out);

    const PCMFormat        m_format_in;
    const PCMFormat        m_format_out;

    const double           m_conversion_ratio;

    // used to convert signed 16 or signed 32 to float 
    std::unique_ptr<float[]> 
                           m_float_buffer_in;
    size_t                 m_last_buffer_in_tsize  = 0;

    // and back
    std::unique_ptr<float[]>
                           m_float_buffer_out;
    size_t                 m_last_buffer_out_tsize = 0;

    typedef std::unique_ptr<SRC_STATE, decltype(&src_delete)> ConverterInstancePtr;
    ConverterInstancePtr     m_converter_inst;
};

#endif //__CONVERTER_H__