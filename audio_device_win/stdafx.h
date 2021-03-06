// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <wmcodecdsp.h>      // CLSID_CWMAudioAEC
// (must be before audioclient.h)
#include <Audioclient.h>     // WASAPI
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>     // MMDevice
#include <avrt.h>            // Avrt
#include <endpointvolume.h>
#include <mediaobj.h>        // IMediaObject
#include <comdef.h>
#include <dmo.h>
#include <mmsystem.h>
#include <strsafe.h>
#include <uuids.h>
#include <guiddef.h>
#include <windows.h>

#include <iostream>
#include <atlcomcli.h>

#include <stdio.h>
#include <tchar.h>
#include <assert.h>

#include <memory>
#include <functional>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <bitset>
#include <fstream>
#include <queue>
#include <list>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>

#include <libsamplerate/src/float_cast.h>
#include <libsamplerate/src/common.h>
#include <samplerate.h>

#include <sample_rate_converter/converter_interface.h>
#include <sample_rate_converter/converter.h>

#include "../sample_rate_converter/converter_interface.h"

#include "com_guard.h"


#define LS_ERROR    "[ ERROR ] : "
#define LS_WARNING  "[WARNING] : "
#define LS_INFO     "[ INFO  ] : "
#define LS_VERBOSE  "[VERBOSE] : "
#define LS_TRACE    "[ TRACE ] : "

#define RTC_LOG(_MESSAGE_)    std::wcout << _MESSAGE_ << std::endl;

#define LOG_ERROR(__MESSAGE__)    RTC_LOG(LS_ERROR    << __MESSAGE__ )
#define LOG_WARNING(__MESSAGE__)  RTC_LOG(LS_WARNING  << __MESSAGE__ )
#define LOG_INFO(__MESSAGE__)     RTC_LOG(LS_INFO     << __MESSAGE__ )
//#define LOG_VERBOSE(__MESSAGE__)  RTC_LOG(LS_VERBOSE  << __MESSAGE__ )
//#define LOG_TRACE(__MESSAGE__)    RTC_LOG(LS_TRACE    << __MESSAGE__ )

#ifndef LOG_ERROR
#define LOG_ERROR(__MESSAGE__)      0
#endif

#ifndef LOG_WARNING
#define LOG_WARNING(__MESSAGE__)    0
#endif

#ifndef LOG_INFO
#define LOG_INFO(__MESSAGE__)       0
#endif

#ifndef LOG_VERBOSE
#define LOG_VERBOSE(__MESSAGE__)    0
#endif 

#ifndef LOG_TRACE
#define LOG_TRACE(__MESSAGE__)      0
#endif