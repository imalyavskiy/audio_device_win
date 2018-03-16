#include "stdafx.h"
#include "AudioDeviceBufferInterface.h"
#include "AudioDeviceGeneric.h"

/*
*  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
*
*  Use of this source code is governed by a BSD-style license
*  that can be found in the LICENSE file in the root of the source
*  tree. An additional intellectual property rights grant can be found
*  in the file PATENTS.  All contributing project authors may
*  be found in the AUTHORS file in the root of the source tree.
*/

//#include "modules/audio_device/audio_device_generic.h"
//#include "rtc_base/logging.h"

int32_t AudioDeviceGenericInterface::EnableBuiltInAEC(bool enable) {
    LOG_ERROR( L"Not supported on this platform");
    return -1;
}

int32_t AudioDeviceGenericInterface::EnableBuiltInAGC(bool enable) {
    LOG_ERROR( L"Not supported on this platform");
    return -1;
}

int32_t AudioDeviceGenericInterface::EnableBuiltInNS(bool enable) {
    LOG_ERROR( L"Not supported on this platform");
    return -1;
}