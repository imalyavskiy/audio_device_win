#include "stdafx.h"
#include "converter_interface.h"
#include "converter.h"

bool CreateConverter(const PCMFormat& format_in, const PCMFormat& format_out, std::shared_ptr<ConverterInterface>& p)
{
    assert((format_in.bitsPerSample % 8) == 0);
    assert((format_out.bitsPerSample % 8) == 0);

    p.reset();

    std::shared_ptr<Converter> _p = std::make_shared<Converter>(format_in, format_out);
    if(!_p->initialize())
        return false;

    return (bool)(p = std::static_pointer_cast<ConverterInterface>(_p));
}

void
Converter::uint8_to_float_array(const uint8_t *in, float *out, int len)
{
    while (len)
    {
        len--;
        out[len] = (float)(in[len] / (1.0 * 0x80) - 1);
    };
}

void
Converter::int16_to_float_array(const int16_t *in, float *out, int len)
{
    while (len)
    {
        len--;
        out[len] = (float)(in[len] / (1.0 * 0x8000));
    };

    return;
}

void 
Converter::int24_to_float_array(const int8_t *in, float *out, int len)
{
    throw std::exception("not implemented");
}

void
Converter::int32_to_float_array(const int32_t *in, float *out, int len)
{
    while (len)
    {
        len--;
        out[len] = (float)(in[len] / (8.0 * 0x10000000));
    };

    return;
}

void 
Converter::float_to_uint8_array(const float *in, uint8_t *out, int len)
{
    double scaled_value;

    while (len)
    {
        len--;

        scaled_value = in[len] * (8.0 * 0x10000000);
        if (CPU_CLIPS_POSITIVE == 0 && scaled_value >= (1.0 * 0x7FFFFFFF))
        {
            out[len] = 32767;
            continue;
        };
        if (CPU_CLIPS_NEGATIVE == 0 && scaled_value <= (-8.0 * 0x10000000))
        {
            out[len] = -32768;
            continue;
        };

        out[len] = (short)(lrint(scaled_value) >> 24);
    };
}

void
Converter::float_to_int16_array(const float *in, int16_t *out, int len)
{
    double scaled_value;

    while (len)
    {
        len--;

        scaled_value = in[len] * (8.0 * 0x10000000);
        if (CPU_CLIPS_POSITIVE == 0 && scaled_value >= (1.0 * 0x7FFFFFFF))
        {
            out[len] = 32767;
            continue;
        };
        if (CPU_CLIPS_NEGATIVE == 0 && scaled_value <= (-8.0 * 0x10000000))
        {
            out[len] = -32768;
            continue;
        };

        out[len] = (short)(lrint(scaled_value) >> 16);
    };

}

void
Converter::float_to_int24_array(const float *in, int8_t *out, int len)
{
    throw std::exception("not implemented");
}

void
Converter::float_to_int32_array(const float *in, int32_t *out, int len)
{
    double scaled_value;

    while (len)
    {
        len--;

        scaled_value = in[len] * (8.0 * 0x10000000);
        if (CPU_CLIPS_POSITIVE == 0 && scaled_value >= (1.0 * 0x7FFFFFFF))
        {
            out[len] = 0x7fffffff;
            continue;
        };
        if (CPU_CLIPS_NEGATIVE == 0 && scaled_value <= (-8.0 * 0x10000000))
        {
            out[len] = -1 - 0x7fffffff;
            continue;
        };

        out[len] = lrint(scaled_value);
    };

}

Converter::Converter(const PCMFormat& format_in, const PCMFormat& format_out)
    : m_converter_inst(nullptr, nullptr)
    , m_format_in(format_in)
    , m_format_out(format_out)
    , m_conversion_ratio((double)format_out.samplesPerSecond / (double)format_in.samplesPerSecond)
{
    // only samples per second can differ
    assert(format_in.channels == format_out.channels);
}

Converter::~Converter()
{
}

bool Converter::initialize()
{
    int error = SRC_ERR_NO_ERROR;

    m_converter_inst = ConverterInstancePtr(src_new(SRC_SINC_FASTEST, m_format_in.channels, &error), &src_delete);
    if (SRC_ERR_NO_ERROR != error )
        return false;

    return (SRC_ERR_NO_ERROR == src_set_ratio(m_converter_inst.get(), m_conversion_ratio));
}

void Converter::update_proxy_buffers(const PCMDataBuffer& buffer_in, const PCMDataBuffer& buffer_out)
{
    // if input samples are not ieee floats then
    if(PCMFormat::ui8 <= m_format_in.sampleFormat && m_format_in.sampleFormat <= PCMFormat::i32)
    {   // allocate proxy input buffer
        if ((!m_float_buffer_in) || (m_last_buffer_in_tsize != buffer_in.total_size))
        {
            const size_t in_buffer_capacity_in_samples
                = buffer_in.total_size / (m_format_in.bytesPerFrame / m_format_in.channels);

            m_float_buffer_in.reset(new float[in_buffer_capacity_in_samples]);

            m_last_buffer_in_tsize = buffer_in.total_size;
        }
    }

    // if output samples are not ieee floats then
    if (PCMFormat::ui8 <= m_format_out.sampleFormat && m_format_out.sampleFormat <= PCMFormat::i32) 
    {   // allocate proxy output buffer
        if ((!m_float_buffer_out) || (m_last_buffer_out_tsize != buffer_out.total_size))
        {
            const size_t out_buffer_capacity_in_samples
                = buffer_out.total_size / (m_format_out.bytesPerFrame / m_format_out.channels);

            m_float_buffer_out.reset(new float[out_buffer_capacity_in_samples]);

            m_last_buffer_out_tsize = buffer_out.total_size;
        }
    }
}

bool Converter::convert(PCMDataBuffer& buffer_in, PCMDataBuffer& buffer_out, bool no_more_data)
{
    // only samples per second can differ
    if (m_format_in.channels != m_format_out.channels)
        return false;

    // buffers must be of size capable to store integral frame count
    assert(0 == buffer_in.actual_size % m_format_in.bytesPerFrame);
    assert(0 == buffer_in.total_size % m_format_in.bytesPerFrame);

    // calculate number of input samples 
    const size_t actual_input_samples = buffer_in.actual_size / (m_format_in.bytesPerFrame / m_format_in.channels);

    // (re)allocate intermediate float[] buffers is needed
    update_proxy_buffers(buffer_in, buffer_out);

    // copy data
    if (m_format_in.sampleFormat == PCMFormat::ui8)
        uint8_to_float_array((uint8_t*)buffer_in.p.get(), m_float_buffer_in.get(), (int32_t)actual_input_samples);
    else if (m_format_in.sampleFormat == PCMFormat::i16)
        int16_to_float_array((int16_t*)buffer_in.p.get(), m_float_buffer_in.get(), (int32_t)actual_input_samples);
    else if (m_format_in.sampleFormat == PCMFormat::i32)
        int32_to_float_array((int32_t*)buffer_in.p.get(), m_float_buffer_in.get(), (int32_t)actual_input_samples);
    else if (m_format_in.sampleFormat == PCMFormat::flt)
        /*nothing to do here*/;
    else
        return false;

    // fill convert request structure
    SRC_DATA src_data
    {
        (m_format_in.sampleFormat == PCMFormat::flt ?           // data_in
            (const float*)buffer_in.p.get() : m_float_buffer_in.get()),
        (m_format_out.sampleFormat == PCMFormat::flt ?          // data_out
            (float*)buffer_out.p.get() : m_float_buffer_out.get()),
        buffer_in.actual_size / m_format_in.bytesPerFrame,      // input_frames     - actual number
        buffer_out.total_size / m_format_out.bytesPerFrame,     // output_frames    - maximum buffer capacity
        0L,                                                     // input_frames_used
        0L,                                                     // output_frames_gen
        (int)no_more_data,                                      // end_of_input
        m_conversion_ratio,                                     // src_ratio
    };

    // process
    int error = SRC_ERR_NO_ERROR;
    if (SRC_ERR_NO_ERROR != (error = src_process(m_converter_inst.get(), &src_data)))
        return false;
  
    // copy data
    if (m_format_out.sampleFormat == PCMFormat::ui8)
        float_to_uint8_array(m_float_buffer_out.get(), (uint8_t*)buffer_out.p.get(), src_data.output_frames_gen * m_format_out.channels);
    else if (m_format_out.sampleFormat == PCMFormat::i16)
        float_to_int16_array(m_float_buffer_out.get(), (int16_t*)buffer_out.p.get(), src_data.output_frames_gen * m_format_out.channels);
    else if (m_format_out.sampleFormat == PCMFormat::i32)
        float_to_int32_array(m_float_buffer_out.get(), (int32_t*)buffer_out.p.get(), src_data.output_frames_gen * m_format_out.channels);
    
    // calculate output data actual size
    buffer_out.actual_size = src_data.output_frames_gen * m_format_out.bytesPerFrame;

    // calc data rest
    const uint32_t bytes_consumed = src_data.input_frames_used * m_format_in.bytesPerFrame;
    buffer_in.actual_size -= bytes_consumed;
    if(buffer_in.actual_size != 0) // move unprocessed data if any to the beginning of the input buffer
        memmove(buffer_in.p.get(), (void*)((char*)buffer_in.p.get() + bytes_consumed), buffer_in.actual_size);

    return true;
}
