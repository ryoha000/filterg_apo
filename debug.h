#pragma once

#ifdef OutputDebugString
#undef OutputDebugString
#endif
#ifdef UNICODE
#define OutputDebugString  OutputDebugStringFW
#define OutputDebugStringV OutputDebugStringFVW
#else
#define OutputDebugString  OutputDebugStringFA
#define OutputDebugStringV OutputDebugStringFVA
#endif // !UNICODE
void OutputDebugStringFA(const char* format, ...);
void OutputDebugStringFW(const wchar_t* format, ...);
