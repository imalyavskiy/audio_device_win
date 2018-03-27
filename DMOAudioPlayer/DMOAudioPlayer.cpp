// DMOAudioPlayer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "common.h"
#include "AudioSynth.h"
#include "AudioDeviceBufferInterface.h"
#include "AudioDeviceBuffer.h"
#include "AudioDeviceGeneric.h"
#include "AudioDeviceWindowsCore.h"
#include "PcmStreamRendererInterface.h"
#include "AudioSourceInterface.h"
#include "AudioSource.h"
#include "PcmStreamRenderer.h"


// int main()
// {
//     AudioDevicePlayoutBufferInterface::ptr outBuffer 
//         = std::static_pointer_cast<AudioDevicePlayoutBufferInterface>(std::make_shared<AudioDeviceBuffer>());
// 
//     std::shared_ptr<AudioDevicePlayoutInterface> playout_device(new AudioDeviceWindowsCore);
// 
//     int32_t r = 0;
// 
//     auto result = playout_device->Init();
// 
//     int16_t outputs = playout_device->PlayoutDevices();
//     if (outputs < 0)
//     {
//         LOG_ERROR( L"No playout devices present");
//         return 1;
//     }
// 
//     for (int16_t cDevice = 0; cDevice < outputs; ++cDevice)
//     {
//         std::wstring name, id;
//         if (playout_device->PlayoutDeviceName((uint16_t)cDevice, name, id) != cDevice)
//         {
//             LOG_INFO(cDevice << L" - No such device index.");
//         }
//         else
//         {
//             LOG_INFO(cDevice << L" - " << name);
//         }
//     }
// 
// 
//     playout_device->AttachAudioBuffers(outBuffer, nullptr);
// 
//     r = playout_device->SetPlayoutDevice(0);
// 
//     r = playout_device->InitPlayout();
// 
//     bool     speaker_volume_is_avaliable = false;
//     uint32_t speaker_volume = 0;
//     bool     speaker_mute = false;
//     uint32_t max_speaker_volume = 0;
//     uint32_t min_speaker_volume = 0;
// 
//     r = playout_device->SpeakerVolumeIsAvailable(speaker_volume_is_avaliable);
// 
//     if (speaker_volume_is_avaliable)
//     {
//         r = playout_device->SpeakerVolume(speaker_volume);
// 
//         r = playout_device->SpeakerMute(speaker_mute);
//         
//         r = playout_device->MaxSpeakerVolume(max_speaker_volume);
//         
//         r = playout_device->MinSpeakerVolume(min_speaker_volume);
//     }
// 
//     if (!playout_device->SpeakerIsInitialized())
//         LOG_ERROR("Speaker is not initialized.");
//     LOG_INFO("Speaker initialized.");
// 
//     r = playout_device->StartPlayout();
// 
//     Sleep(5000);
// 
//     r = playout_device->StopPlayout();
// 
//     return 0;
// }

HRESULT PlayAudioStream(WavAudioSource::Interface *pMySource);

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cout << "Please provide a headed wav file" << std::endl;
        return 1;
    }

    WavAudioSource::Interface::ptr source;
    if (!WavAudioSource::create(std::string(argv[1]), source))
    {
        std::cout << "Failed to create wav file source." << std::endl;
        return 1;
    }

    PCMFormat file_PCM_format{0,0,0,0};
    if(!source->GetFormat(file_PCM_format))
        return 1;

#if 0
    PlayAudioStream(source.get());
#else
    PcmSrtreamRenderer::Interface::ptr renderer;
    if (!PcmSrtreamRenderer::create(renderer))
    {
        std::cout << "Failed to create PCM renderer." << std::endl;
        return 1;
    }

    if(!renderer->SetFormat(file_PCM_format, file_PCM_format.samplesPerSecond / 10, 10))
        return 1;

    auto dispatch_thread_func = [](WavAudioSource::Interface::ptr source, PcmSrtreamRenderer::Interface::ptr renderer)->bool
    {
        std::weak_ptr<PCMDataBuffer> wbuffer;

        while (true)
        {
            if (!renderer->GetBuffer(wbuffer) || wbuffer.expired())
                return false;

            std::shared_ptr<PCMDataBuffer> sbuffer(wbuffer.lock());
            if (!source->ReadData(sbuffer))
                return false;

            if (!renderer->PutBuffer(wbuffer))
                return false;
        }

        return true;
    };

    std::thread dispatch(dispatch_thread_func, source, renderer);

    dispatch.join();

    std::cout << "Dispatch thread finished" << std::endl;

#endif
    return 0;
}
