#include "stdafx.h"
#include "AudioDeviceBuffer.h"
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

namespace webrtc {

    bool AudioDeviceGeneric::BuiltInAECIsAvailable() const {
        RTC_LOG(LS_ERROR << "Not supported on this platform");
        return false;
    }

    int32_t AudioDeviceGeneric::EnableBuiltInAEC(bool enable) {
        RTC_LOG(LS_ERROR << "Not supported on this platform");
        return -1;
    }

    bool AudioDeviceGeneric::BuiltInAGCIsAvailable() const {
        RTC_LOG(LS_ERROR << "Not supported on this platform");
        return false;
    }

    int32_t AudioDeviceGeneric::EnableBuiltInAGC(bool enable) {
        RTC_LOG(LS_ERROR << "Not supported on this platform");
        return -1;
    }

    bool AudioDeviceGeneric::BuiltInNSIsAvailable() const {
        RTC_LOG(LS_ERROR << "Not supported on this platform");
        return false;
    }

    int32_t AudioDeviceGeneric::EnableBuiltInNS(bool enable) {
        RTC_LOG(LS_ERROR << "Not supported on this platform");
        return -1;
    }
}  // namespace webrtc
