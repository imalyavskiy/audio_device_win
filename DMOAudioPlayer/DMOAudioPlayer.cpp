// DMOAudioPlayer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "AudioDeviceBufferInterface.h"
#include "AudioDeviceBuffer.h"
#include "AudioDeviceGeneric.h"
#include "AudioDeviceWindowsCore.h"

#define LIST_OUTPUTS(__ADAPTER__)                                                    \
{                                                                                    \
    for (int16_t cDevice = 0; cDevice < outputs; ++cDevice){                         \
        std::wstring name, id;                                                       \
        if (__ADAPTER__.PlayoutDeviceName((uint16_t)cDevice, name, id) != cDevice)   \
            LOG_ERROR( L"No such device index: " << cDevice);               \
                                                                                     \
        LOG_INFO( L"Device: " << name << L"(" << id << L")");               \
    }                                                                                \
}


int main()
{
    AudioDeviceBufferInterface::ptr buffer(new AudioDeviceBuffer());

    AudioDeviceWindowsCore device;

    auto result = device.Init();

    int16_t outputs = device.PlayoutDevices();
    if (outputs < 0)
    {
        LOG_ERROR( L"No playout devices present");
        return 1;
    }

//  LIST_OUTPUTS(device);

    device.AttachAudioBuffer(buffer);

    device.SetPlayoutDevice(0);

    device.InitPlayout();

    device.StartPlayout();

    return 0;
}

