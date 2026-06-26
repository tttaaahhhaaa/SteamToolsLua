#ifndef HANDLERS_H
#define HANDLERS_H

#include <windows.h>

char *DispatchCommand(const char *cmd, const char *args);
void SetExeDir(const wchar_t *d);

#endif
