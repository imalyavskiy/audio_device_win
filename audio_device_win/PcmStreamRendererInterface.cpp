#include "stdafx.h"
#include "common.h"
#include "SampleRateConverterInterface.h"
#include "PcmStreamRendererInterface.h"
#include "PcmStreamRenderer.h"

bool create(const std::string& dump_file, std::shared_ptr<IPcmSrtreamRenderer>& instance)
{
    std::shared_ptr<PcmSrtreamRenderer> p = std::make_shared<PcmSrtreamRenderer>(dump_file);
    if (!p->Init())
        return false;

    instance = std::static_pointer_cast<IPcmSrtreamRenderer>(p);
    return bool(instance);
}
