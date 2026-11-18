#include "putty.h"

#undef GetModuleFileNameA
#undef GetModuleFileNameW

DWORD safeGetModuleFileNameA(HMODULE module, LPSTR filename, DWORD size)
{
    DWORD written = GetModuleFileNameA(module, filename, size);
    if (size)
        filename[size - 1] = '\0'; // Windows XP fix
    if (!written && size)
        *filename = '\0';
    return written;
}

DWORD safeGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size)
{
    DWORD written = GetModuleFileNameW(module, filename, size);
    if (size)
        filename[size - 1] = L'\0'; // Windows XP fix
    if (!written && size)
        *filename = L'\0';
    return written;
}
