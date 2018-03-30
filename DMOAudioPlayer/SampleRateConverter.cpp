#include "stdafx.h"
#include "common.h"
#include "SampleRateConverterInterface.h"
#include "SampleRateConverter.h"

namespace SampleRateConverter
{
    Implementation::Implementation()
        : m_input_flow(new common::DataFlow)
        , m_output_flow(new common::DataFlow)
    {
        ;
    }

    Implementation::~Implementation()
    {
        ;
    }

    bool 
    Implementation::GetInputDataFlow(common::DataFlowInterface::wptr& p)
    {
        if(!m_input_flow->inputPort(p))
            return false;

        return !p.expired();
    }

    bool 
    Implementation::SetInputFormat(std::shared_ptr<const PCMFormat>& format, const size_t buffer_frames, const size_t buffers_total)
    {
        // keep format input format data
        m_format_input = format;

        // calculate single buffer size in bytes
        const size_t buffer_size = m_format_input->bytesPerFrame * buffer_frames;

        if (!m_input_flow->Alloc(buffer_size, buffers_total))
            return false;

        if (*m_format_output == *m_format_input)
        {
            m_output_flow = m_input_flow;
        }
        else
        {
            // TODO: implement converter instantiation
            throw std::exception("not implemented");
        }

        return true;
    }

    bool 
    Implementation::GetInputFormat(std::shared_ptr<const PCMFormat>& format, size_t& buffer_frames, size_t& buffers_total) const
    {
        if (!m_format_input)
            return false;
        
        format = m_format_input;

        return true;
    }

    bool 
    Implementation::GetOutputDataFlow(common::DataFlowInterface::wptr& p)
    {
        if (!m_output_flow->outputPort(p))
            return false;
        
        return !p.expired();
    }

    bool 
    Implementation::SetOutputFormat(std::shared_ptr<const PCMFormat>& f)
    {
        m_format_output = f;

        return (bool)m_format_output;
    }

    bool 
    Implementation::GetOutputFormat(std::shared_ptr<const PCMFormat>& f) const
    {
        if (!m_format_output)
            return false;
        
        f = m_format_output;

        return (bool)f;
    }
}