#ifndef __SAMPLE_RATE_CONVERTER_INTERFACE_H__
#define __SAMPLE_RATE_CONVERTER_INTERFACE_H__
#pragma once
struct ISampleRateConverter
{
    typedef std::shared_ptr<ISampleRateConverter> ptr;

    virtual bool GetInputDataPort(common::DataPortInterface::wptr& p) = 0;
    virtual bool GetOutputDataPort(common::DataPortInterface::wptr& p) = 0;

    virtual bool SetFormats(const std::shared_ptr<PCMFormat>& in, const std::shared_ptr<PCMFormat>& out) = 0;
    virtual bool GetFormats(std::shared_ptr<const PCMFormat>& in, std::shared_ptr<const PCMFormat>& out) const = 0;
};

bool create(std::shared_ptr<ISampleRateConverter>& instance);
#endif // __SAMPLE_RATE_CONVERTER_INTERFACE_H__
