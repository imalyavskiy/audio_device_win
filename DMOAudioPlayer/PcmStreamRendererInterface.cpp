#include "stdafx.h"
#include "common.h"
#include "PcmStreamRendererInterface.h"
#include "PcmStreamRenderer.h"
namespace PcmSrtreamRenderer {
    bool create(std::shared_ptr<Interface>& instance)
    {
        std::shared_ptr<Implementation> p = std::make_shared<Implementation>();
        if (!p->Init())
            return false;

        instance = std::static_pointer_cast<Interface>(p);
        return bool(instance);
    }
}