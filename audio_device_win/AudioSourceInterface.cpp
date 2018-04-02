#include "stdafx.h"
#include "common.h"
#include "AudioSourceInterface.h"
#include "AudioSource.h"

bool create(const std::string& file, std::shared_ptr<IWavAudioSource>& source)
{
    WavAudioSource* p = new WavAudioSource();

    if (!p->Init(file))
        return false;

    source.reset(p);

    return (bool)source;
}
