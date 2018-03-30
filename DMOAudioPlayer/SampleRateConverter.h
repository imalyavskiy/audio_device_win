#ifndef __SAMPLE_RATE_CONVERTER_H__
#define __SAMPLE_RATE_CONVERTER_H__
#pragma once

namespace SampleRateConverter
{
    class Implementation
        : public Interface
    {
    public:
        Implementation();
        ~Implementation();

        // SampleRateConverterInterface
        bool GetInputDataFlow(common::DataFlowInterface::wptr& p) override;
        bool SetInputFormat(std::shared_ptr<const PCMFormat>& format, const size_t buffer_frames, const size_t buffers_total) override;
        bool GetInputFormat(std::shared_ptr<const PCMFormat>& format, size_t& buffer_frames, size_t& buffers_total) const override;
        
        bool GetOutputDataFlow(common::DataFlowInterface::wptr& p) override;
        bool SetOutputFormat(std::shared_ptr<const PCMFormat>& f) override;
        bool GetOutputFormat(std::shared_ptr<const PCMFormat>& f) const override;

    protected:
        std::shared_ptr<const PCMFormat>  m_format_input;
        std::shared_ptr<const PCMFormat>  m_format_output;

        // Input flow
        std::shared_ptr<common::DataFlow> m_input_flow;

        // Output flow
        std::shared_ptr<common::DataFlow> m_output_flow;
    };
}

#endif // __SAMPLE_RATE_CONVERTER_H__
