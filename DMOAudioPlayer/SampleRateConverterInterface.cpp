#include "stdafx.h"
#include "common.h"
#include "SampleRateConverterInterface.h"
#include "SampleRateConverter.h"
namespace SampleRateConverter
{
    bool create(std::shared_ptr<Interface>& instance)
    {
        std::shared_ptr<Implementation> p = std::make_shared<Implementation>();

        instance = std::static_pointer_cast<Interface>(p);
        return bool(instance);
    }
}
