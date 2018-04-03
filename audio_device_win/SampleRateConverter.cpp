#include "stdafx.h"
#include "common.h"
#include "SampleRateConverterInterface.h"
#include "SampleRateConverter.h"

SampleRateConverter::SampleRateConverter()
    : m_input_flow(new common::DataFlow)
    , m_output_flow(new common::DataFlow)
{
    ;
}

SampleRateConverter::~SampleRateConverter()
{
    if (m_convert_thread.joinable())
    {
        m_convert_thread_interraptor.activate();

        m_convert_thread.join();
    }
}

bool 
SampleRateConverter::GetInputDataPort(common::DataPortInterface::wptr& p)
{
    if(!m_input_flow->inputPort(p))
        return false;

    return !p.expired();
}

bool 
SampleRateConverter::GetOutputDataPort(common::DataPortInterface::wptr& p)
{
    if (!m_output_flow->outputPort(p))
        return false;
        
    return !p.expired();
}

bool 
SampleRateConverter::SetFormats(const std::shared_ptr<PCMFormat>& in, const std::shared_ptr<PCMFormat>& out)
{
    // keep format input format data
    if (!in || !out)
        return false;
        
    m_format_input = in;
    m_format_output = out;

    if (!InitBuffers())
        return false;
    
    return true;
}

bool 
SampleRateConverter::GetFormats(std::shared_ptr<const PCMFormat>& in, std::shared_ptr<const PCMFormat>& out) const
{
    if (!m_format_input || !m_format_output)
        return false;
        
    in = m_format_input;
    out = m_format_output;

    return true;
}

bool SampleRateConverter::InitBuffers()
{
    // calculate single buffer size in bytes
    const size_t input_buffer_size = m_format_input->bytesPerFrame * m_format_input->samplesPerSecond / 2;

    if (!m_input_flow->Alloc(input_buffer_size, m_buffers_total))
        return false;

    if (*m_format_output == *m_format_input)
    {
        m_output_flow = m_input_flow;

        return true;
    }

    const size_t output_buffer_size = m_format_output->bytesPerFrame * m_format_output->samplesPerSecond / 2;
    if (!m_output_flow->Alloc(output_buffer_size, m_buffers_total))
        return false;

    return InitConversion();
}

bool SampleRateConverter::InitConversion()
{
    int error = SRC_ERR_NO_ERROR;

    const double conversion_ratio = (double)m_format_output->samplesPerSecond / (double)m_format_input->samplesPerSecond;

    if (!CreateConverter(*m_format_input, *m_format_output, m_converter_impl))
    {
        std::cout << "Error: Failed to initialize converter." << std::endl;
        return false;
    }
        
    common::DataPortInterface::wptr input_data;
    m_input_flow->outputPort(input_data);

    common::DataPortInterface::wptr output_data;
    m_output_flow->inputPort(output_data);

    {
        std::unique_lock<std::mutex> lock(m_convert_thread_mtx);
        m_convert_thread = std::thread(std::bind(&SampleRateConverter::DoConvert, this, std::placeholders::_1, std::placeholders::_2), input_data, output_data);
        m_convert_thread_cv.wait(lock);
    }

    return true;
}

bool SampleRateConverter::DoConvert(common::DataPortInterface::wptr in, common::DataPortInterface::wptr out)
{
    bool eos = false;

    while(!eos)
    {
        if (in.expired() || out.expired())
            break;

        std::shared_ptr<common::DataPortInterface> in_ = in.lock();
        std::shared_ptr<common::DataPortInterface> out_ = out.lock();

        std::weak_ptr<PCMDataBuffer> wbuffer_in;
        std::weak_ptr<PCMDataBuffer> wbuffer_out;

        {
            std::unique_lock<std::mutex> l(m_convert_thread_mtx);
            m_convert_thread_cv.notify_all();
        }

        do {
            if (!in_->GetBuffer(wbuffer_in))
                continue;
            else
                break;
        } while (!m_convert_thread_interraptor.wait());

        do {
            if (!out_->GetBuffer(wbuffer_out))
                continue;
            else
                break;
        } while (!m_convert_thread_interraptor.wait());

        {
            std::shared_ptr<PCMDataBuffer> buffer_in = wbuffer_in.lock();
            std::shared_ptr<PCMDataBuffer> buffer_out = wbuffer_out.lock();

            if (!m_converter_impl->convert(*buffer_in, *buffer_out, eos = buffer_in->end_of_stream))
                break;
                
            buffer_out->end_of_stream = buffer_in->end_of_stream;

            assert(buffer_in->actual_size == 0);
        }

        if (!in_->PutBuffer(wbuffer_in))
            break;
            
        if (!out_->PutBuffer(wbuffer_out))
            break;
    }

    if (m_convert_thread_completor)
        m_convert_thread_completor.complete();
    else
        m_convert_thread_interraptor.wait(std::chrono::milliseconds(500));

    return true;
}