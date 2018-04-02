#ifndef __SAMPLE_RATE_CONVERTER_INTERFACE_H__
#define __SAMPLE_RATE_CONVERTER_INTERFACE_H__
#pragma once
namespace SampleRateConverter
{
    struct Interface
    {
        typedef std::shared_ptr<Interface> ptr;

        virtual bool GetInputDataPort(common::DataPortInterface::wptr& p) = 0;
        virtual bool SetInputFormat(std::shared_ptr<const PCMFormat>& f) = 0;
        virtual bool GetInputFormat(std::shared_ptr<const PCMFormat>& f) const = 0;

        virtual bool GetOutputDataPort(common::DataPortInterface::wptr& p) = 0;
        virtual bool SetOutputFormat(std::shared_ptr<const PCMFormat>& f) = 0;
        virtual bool GetOutputFormat(std::shared_ptr<const PCMFormat>& f) const = 0;
    };

    bool create(std::shared_ptr<Interface>& instance);
}
#endif // __SAMPLE_RATE_CONVERTER_INTERFACE_H__