#include "stdafx.h"
#include "common.h"
#include "AudioSourceInterface.h"
#include "AudioSource.h"

namespace WavAudioSource{
    bool create(const std::string& file, std::shared_ptr<Interface>& source)
    {
        Implementation* p = new Implementation();

        if (!p->Init(file))
            return false;

        source.reset(p);

        return (bool)source;
    }
}
