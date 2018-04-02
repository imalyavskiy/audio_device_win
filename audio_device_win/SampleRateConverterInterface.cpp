#include "stdafx.h"
#include "common.h"
#include "SampleRateConverterInterface.h"
#include "SampleRateConverter.h"
bool create(std::shared_ptr<ISampleRateConverter>& instance)
{
    std::shared_ptr<SampleRateConverter> p = std::make_shared<SampleRateConverter>();

    instance = std::static_pointer_cast<ISampleRateConverter>(p);
    return bool(instance);
}