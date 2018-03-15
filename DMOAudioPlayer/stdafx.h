// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <windows.h>
#include <iostream>
#include <atlcomcli.h>

#include <stdio.h>
#include <tchar.h>
#include <assert.h>

#include <memory>
#include <functional>
#include <string>

#include "critical_section.h"
#include "com_guard.h"
#include "AudioDeviceModule.h"


#define LS_VERBOSE  "[VERBOSE] : "
#define LS_ERROR    "[ ERROR ] : "
#define LS_WARNING  "[WARNING] : "
#define LS_INFO     "[ INFO  ] : "
#define LS_TRACE    "[ TRACE ] : "

#define RTC_LOG(_X_)    std::wcout << _X_ << std::endl;

// TODO: reference additional headers your program requires here
