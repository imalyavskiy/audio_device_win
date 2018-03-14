// DMOAudioPlayer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "AudioDeviceBuffer.h"
#include "AudioDeviceGeneric.h"
#include "AudioDeviceWindowsCore.h"

int main()
{
    webrtc::AudioDeviceWindowsCore device;

    auto result = device.Init();

    int16_t outputs = device.PlayoutDevices();
    if (outputs < 0)
    {
        RTC_LOG(LS_ERROR << "No playout devices present");
        return 1;
    }

//     for (int16_t cDevice = 0; cDevice < outputs; ++cDevice)
//     {
//         std::string name;
//         std::string id;
//         if (device.PlayoutDeviceName((uint16_t)cDevice, name, id) != cDevice)
//             RTC_LOG(LS_ERROR << "No such device index: " << cDevice);
// 
//         RTC_LOG(LS_INFO << "Device: " << name << "(" << id << ")");
//     }

    device.SetPlayoutDevice(0);

    device.StartPlayout();

    device.InitPlayout();

    device.SetPlayoutDevice(AudioDeviceModule::kDefaultDevice);

    device.InitSpeaker();

    return 0;
}

