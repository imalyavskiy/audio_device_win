#ifndef __COM_GUARD_H__
#define __COM_GUARD_H__
#pragma once

struct ScopedCOMInitializer
{
    enum SelectMTA { kMTA };

    ScopedCOMInitializer(SelectMTA mta = kMTA);
    ~ScopedCOMInitializer();

    bool succeeded() const { return m_succeeded; };

    ScopedCOMInitializer(ScopedCOMInitializer&&) = delete;
    ScopedCOMInitializer(const ScopedCOMInitializer&) = delete;
    const ScopedCOMInitializer& operator=(ScopedCOMInitializer&&) = delete;
    const ScopedCOMInitializer& operator=(const ScopedCOMInitializer&) = delete;

private:
    const bool m_succeeded;
};

#endif // __COM_GUARD_H__