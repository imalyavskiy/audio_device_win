#ifndef __AUDIO_DEVICE_MODULE_H__
#define __AUDIO_DEVICE_MODULE_H__
#pragma once

struct AudioDeviceModule
{
    enum AudioLayer {
        kPlatformDefaultAudio = 0,
        kWindowsCoreAudio = 2,
        kLinuxAlsaAudio = 3,
        kLinuxPulseAudio = 4,
        kAndroidJavaAudio = 5,
        kAndroidOpenSLESAudio = 6,
        kAndroidJavaInputAndOpenSLESOutputAudio = 7,
        kDummyAudio = 8
    };

    enum WindowsDeviceType {
        kDefaultCommunicationDevice = -1,
        kDefaultDevice = -2
    };
};

#endif // __AUDIO_DEVICE_MODULE_H__
