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

Converter::Converter(const PCMFormat& format_in, const PCMFormat& format_out)
    : m_converter_inst(nullptr)
    , m_format_in(format_in)
    , m_format_out(format_out)
    , m_conversion_ratio((double)format_out.samplesPerSecond / (double)format_in.samplesPerSecond)
{
    // only samples per second can differ
    assert(format_in.bitsPerSample == format_out.bitsPerSample);
    assert(format_in.channels == format_out.channels);
}

Converter::~Converter()
{
    if (m_float_buffer_in)
    {
        delete[] m_float_buffer_in;
        m_float_buffer_in = nullptr;
    }
    
    if (m_float_buffer_out)
    {
        delete[] m_float_buffer_out;
        m_float_buffer_out = nullptr;
    }

    if(m_converter_inst)
    {
        src_delete(m_converter_inst);
        m_converter_inst = nullptr;
    }
}

bool Converter::initialize()
{
    int error = SRC_ERR_NO_ERROR;

    m_converter_inst = src_new(SRC_SINC_FASTEST, m_format_in.channels, &error);
    if (SRC_ERR_NO_ERROR != error )
        return false;

    return (SRC_ERR_NO_ERROR == src_set_ratio(m_converter_inst, m_conversion_ratio));
}

void Converter::update_proxy_buffers(const PCMDataBuffer& buffer_in, const PCMDataBuffer& buffer_out)
{
    // allocate proxy input buffer
    if ((!m_float_buffer_in) || (m_last_buffer_in_tsize != buffer_in.tsize))
    {
        const size_t in_buffer_capacity_in_samples 
            = buffer_in.tsize / (m_format_in.bytesPerFrame / m_format_in.channels);

        if (m_float_buffer_in)
            delete[] m_float_buffer_in;

        m_float_buffer_in = (new float[in_buffer_capacity_in_samples]);

        m_last_buffer_in_tsize = buffer_in.tsize;
    }

    // allocate proxy output buffer
    if ((!m_float_buffer_out) || (m_last_buffer_out_tsize != buffer_out.tsize))
    {
        const size_t out_buffer_capacity_in_samples 
            = buffer_in.tsize / (m_format_out.bytesPerFrame / m_format_out.channels);

        if (m_float_buffer_out)
            delete[] m_float_buffer_out;

        m_float_buffer_out = (new float[out_buffer_capacity_in_samples]);

        m_last_buffer_out_tsize = buffer_out.tsize;
    }
}

bool Converter::convert(PCMDataBuffer& buffer_in, PCMDataBuffer& buffer_out, bool no_more_data)
{
    // only samples per second can differ
    if (m_format_in.bitsPerSample != m_format_out.bitsPerSample)
        return false;
    if (m_format_in.channels != m_format_out.channels)
        return false;

    // buffers must be of size capable to store integral frame count
    assert(0 == buffer_in.asize % m_format_in.bytesPerFrame);
    assert(0 == buffer_in.tsize % m_format_in.bytesPerFrame);

    //
    const size_t actual_input_samples = buffer_in.asize / (m_format_in.bytesPerFrame / m_format_in.channels);

    //
    update_proxy_buffers(buffer_in, buffer_out);

    // copy data
    if (m_format_in.bitsPerSample == 16)        // from input short buffer to proxy float bufer
        src_short_to_float_array((short*)buffer_in.p, m_float_buffer_in, actual_input_samples);
    else if (m_format_in.bitsPerSample == 32)   // from unput int buffer to proxy float bufer
        src_int_to_float_array((int*)buffer_in.p, m_float_buffer_in, actual_input_samples);
    else                                        // error
        return false;

    // Fill convert request structure
    SRC_DATA src_data
    {
        m_float_buffer_in,                              // data_in
        m_float_buffer_out,                             // data_out
        buffer_in.asize / m_format_in.bytesPerFrame,    // input_frames     - actual number
        buffer_out.tsize / m_format_out.bytesPerFrame,  // output_frames    - maximum buffer capacity
        0,                                              // input_frames_used
        0,                                              // output_frames_gen
        (int)no_more_data,                              // end_of_input
        m_conversion_ratio,                             // src_ratio
    };

    // process
    int error = SRC_ERR_NO_ERROR;
    if (SRC_ERR_NO_ERROR != (error = src_process(m_converter_inst, &src_data)))
        return false;
  
    // copy data
    if (m_format_in.bitsPerSample == 16)        // from proxy float buffer to output short buffer
        src_float_to_short_array(m_float_buffer_out, (short*)buffer_out.p, src_data.output_frames_gen * m_format_out.channels);
    else if (m_format_in.bitsPerSample == 32)   // from proxy float buffer to output int buffer
        src_float_to_int_array(m_float_buffer_out, (int*)buffer_out.p, src_data.output_frames_gen * m_format_out.channels);
    
    // 
    buffer_out.asize = src_data.output_frames_gen * m_format_out.bytesPerFrame;

    // calc data rest
    const uint32_t bytes_consumed = src_data.input_frames_used * m_format_in.bytesPerFrame;
    buffer_in.asize -= bytes_consumed;
    if(buffer_in.asize != 0) // move unprocessed data if any to the beginning of the input buffer
        memmove(buffer_in.p, (void*)((char*)buffer_in.p + bytes_consumed), buffer_in.asize);

    return true;
}
