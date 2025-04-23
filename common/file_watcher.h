#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#include "min_windows.h"

void addFileWatcher(LPCSTR path, void *(* callback)(void *), void *arg);
void removeFileWatcher();

#endif
