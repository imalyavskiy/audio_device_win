#include "stdafx.h"

ScopedCOMInitializer::ScopedCOMInitializer(SelectMTA mta)
    : m_succeeded(S_OK == CoInitializeEx(NULL, COINIT_MULTITHREADED))
{ 
    ;
}

ScopedCOMInitializer::~ScopedCOMInitializer()
{ 
    CoUninitialize();
}
