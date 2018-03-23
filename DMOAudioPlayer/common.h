#ifndef __COMMON_H__
#define __COMMON_H__
#pragma once

template<class T>
using ComUniquePtr = std::unique_ptr<T, decltype(&CoTaskMemFree)>;

#endif // __COMMON_H__
