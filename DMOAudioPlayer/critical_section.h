#ifndef __CRITICAL_SECTION_H__
#define __CRITICAL_SECTION_H__
#pragma once

struct CriticalSection
{
    friend struct AutoLock;
    
    CriticalSection();

    ~CriticalSection();

    void Enter();

    void Leave();

    DWORD CurrentOwnerId();

private:
    CRITICAL_SECTION * _pcs;
    DWORD owner;
};

struct AutoLock
{
    AutoLock(CriticalSection* cs);

    AutoLock(CriticalSection& cs);

    ~AutoLock();

private:
    CriticalSection* _cs;
};

#endif // __CRITICAL_SECTION_H__