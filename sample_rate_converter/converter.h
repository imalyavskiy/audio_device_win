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

    void uint8_to_float_array(const uint8_t *in, float *out, int len);
    void int16_to_float_array(const int16_t *in, float *out, int len);
    void int24_to_float_array(const int8_t *in, float *out, int len);
    void int32_to_float_array(const int32_t *in, float *out, int len);

    void float_to_uint8_array(const float *in, uint8_t *out, int len);
    void float_to_int16_array(const float *in, int16_t *out, int len);
    void float_to_int24_array(const float *in, int8_t *out, int len);
    void float_to_int32_array(const float *in, int32_t *out, int len);

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