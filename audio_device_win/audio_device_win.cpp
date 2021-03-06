// audio_device_win.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "common.h"

#include "AudioSynth.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "SampleRateConverterInterface.h"

#include "DataStream.h"

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Please provide a headed wav file" << std::endl;
        return 1;
    }

    IWavAudioSource::ptr source;
    if (!create(std::string(argv[1]), source))
        return 1;

    IPcmSrtreamRenderer::ptr renderer;
    if (!create(argc == 3 ? argv[2] : "", renderer))
        return 1;

    ISampleRateConverter::ptr converter;
    if (!create(converter))
        return 1;

    DataStream streamer(source, converter, renderer);

    if (!streamer.Init())
        return 1;

    if (!streamer.Start())
        return 1;

#if 1
    if(!streamer.WaitForCompletion())
        return 1;
#else
    std::this_thread::sleep_for(std::chrono::seconds(17));
#endif
    return 0;
}
