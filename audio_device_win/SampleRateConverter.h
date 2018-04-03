#ifndef __SAMPLE_RATE_CONVERTER_H__
#define __SAMPLE_RATE_CONVERTER_H__
#pragma once

class SampleRateConverter
    : public ISampleRateConverter
{
public:
    SampleRateConverter();
    ~SampleRateConverter();

    // SampleRateConverterInterface
    bool GetInputDataPort(common::DataPortInterface::wptr& p) override;
    bool GetOutputDataPort(common::DataPortInterface::wptr& p) override;

    bool SetFormats(const std::shared_ptr<PCMFormat>& in, const std::shared_ptr<PCMFormat>& out) override;
    bool GetFormats(std::shared_ptr<const PCMFormat>& in, std::shared_ptr<const PCMFormat>& out) const override;

protected:
    bool InitBuffers();
    bool InitConversion();

    bool DoConvert(common::DataPortInterface::wptr in, common::DataPortInterface::wptr out);

protected:
    std::shared_ptr<const PCMFormat>  m_format_input;
    std::shared_ptr<const PCMFormat>  m_format_output;

    // Input flow
    std::shared_ptr<common::DataFlow> m_input_flow;

    // Output flow
    std::shared_ptr<common::DataFlow> m_output_flow;

    const uint32_t m_buffers_total = 1;

    std::shared_ptr<ConverterInterface> m_converter_impl;
        
    std::thread                         m_convert_thread;
    std::mutex                          m_convert_thread_mtx;
    std::condition_variable             m_convert_thread_cv;
    common::ThreadInterraptor           m_convert_thread_interraptor;
    common::ThreadCompletor             m_convert_thread_completor;
};

#endif // __SAMPLE_RATE_CONVERTER_H__
