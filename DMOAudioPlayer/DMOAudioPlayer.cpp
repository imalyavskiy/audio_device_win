// DMOAudioPlayer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "AudioSynth.h"
#include "AudioDeviceBufferInterface.h"
#include "AudioDeviceBuffer.h"
#include "AudioDeviceGeneric.h"
#include "AudioDeviceWindowsCore.h"

#define LIST_OUTPUTS(__ADAPTER__)                                                    \
{                                                                                    \
    for (int16_t cDevice = 0; cDevice < outputs; ++cDevice){                         \
        std::wstring name, id;                                                       \
        if (__ADAPTER__.PlayoutDeviceName((uint16_t)cDevice, name, id) != cDevice)   \
            LOG_ERROR( L"No such device index: " << cDevice);                        \
                                                                                     \
        LOG_INFO( L"Device: " << name << L"(" << id << L")");                        \
    }                                                                                \
}


int main()
{
    AudioDevicePlayoutBufferInterface::ptr outBuffer 
        = std::static_pointer_cast<AudioDevicePlayoutBufferInterface>(std::make_shared<AudioDeviceBuffer>());

    std::shared_ptr<AudioDevicePlayoutInterface> playout_device(new AudioDeviceWindowsCore);


    auto result = playout_device->Init();

    int16_t outputs = playout_device->PlayoutDevices();
    if (outputs < 0)
    {
        LOG_ERROR( L"No playout devices present");
        return 1;
    }

//  LIST_OUTPUTS(device);

    playout_device->AttachAudioBuffers(outBuffer, nullptr);

    playout_device->SetPlayoutDevice(0);

    playout_device->InitPlayout();

    playout_device->StartPlayout();

    return 0;
}

