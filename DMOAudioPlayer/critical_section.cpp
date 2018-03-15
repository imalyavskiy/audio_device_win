#include "stdafx.h"

CriticalSection::CriticalSection()
    : _pcs(new CRITICAL_SECTION)
{
    InitializeCriticalSection(_pcs);
}

CriticalSection::~CriticalSection()
{
    if (!_pcs)
        return;

    DeleteCriticalSection(_pcs);
    delete _pcs;
    _pcs = nullptr;
}

void 
CriticalSection::Enter()
{
    EnterCriticalSection(_pcs);
}

void 
CriticalSection::Leave()
{
    LeaveCriticalSection(_pcs);
}

HANDLE
CriticalSection::CurrentOwner()
{
    return _pcs->OwningThread;
}

AutoLock::AutoLock(CriticalSection* cs)
    : _cs(cs)
{
    if (_cs)
        _cs->Enter();
}

AutoLock::AutoLock(CriticalSection& cs)
    : _cs(&cs) 
{
    if(_cs)
        _cs->Enter();
}

AutoLock::~AutoLock()
{
    if(_cs)
        _cs->Leave();
}
