// DMOAudioPlayer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "common.h"

#include "AudioSynth.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "DataStream.h"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cout << "Please provide a headed wav file" << std::endl;
        return 1;
    }

    WavAudioSource::Interface::ptr source;
    if (!WavAudioSource::create(std::string(argv[1]), source))
        return 1;

    PcmSrtreamRenderer::Interface::ptr renderer;
    if (!PcmSrtreamRenderer::create(argc == 3 ? argv[2] : "", renderer))
        return 1;

    DataStream streamer(source, renderer);

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
