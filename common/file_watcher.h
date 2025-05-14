#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#include <string>

void addFileWatcher(std::string path, void *(* callback)(void *), void *arg);
void removeFileWatcher();

#endif
