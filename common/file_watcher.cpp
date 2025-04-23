#include "file_watcher.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <immintrin.h>

#include "min_windows.h"

struct thread_args
{
    LPCSTR path;
    void *(* callback)(void *);
    void *arg;
};

static pthread_t fileWatcherThread;
static thread_args threadArgs{};
static bool fileWatcherRunning = false;

static HANDLE hDir;
// static OVERLAPPED overlapped;

void fileWatcherInit(thread_args *args)
{
    const auto [path, callback, arg] = *args;
    hDir = FindFirstChangeNotification(path, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION);
    if (hDir == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to open directory \"%s\" with error code: %x\n", path, HRESULT_FROM_WIN32(GetLastError()));
        return;
    }

    // hDir = CreateFileW(path,
    //     FILE_LIST_DIRECTORY,
    //     FILE_SHARE_READ,
    //     nullptr,
    //     OPEN_EXISTING,
    //     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
    //     nullptr
    // );
    //
    // if (hDir == INVALID_HANDLE_VALUE)
    // {
    //     fprintf(stderr, "Failed to open directory \"%ls\"\n", path);
    //     return;
    // }
    //
    // FILE_NOTIFY_INFORMATION buffer[1024];
    // DWORD bytesReturned;
    //
    // ZeroMemory(&overlapped, sizeof(overlapped));
    // overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    // if (overlapped.hEvent == nullptr)
    // {
    //     fprintf(stderr, "Failed to create event\n");
    //     CloseHandle(hDir);
    //     return;
    // }

    while (fileWatcherRunning)
    {
        switch (WaitForSingleObject(hDir, INFINITE))
        {
            case WAIT_OBJECT_0:
            case WAIT_OBJECT_0 + 1:
                callback(arg);
                break;
            default:
                fprintf(stderr, "Failed to wait for file \"%s\"\n", path);
                break;
        }

        FindNextChangeNotification(hDir);
        // WINBOOL success = ReadDirectoryChangesW(
        //     hDir,
        //     buffer,
        //     sizeof(buffer),
        //     TRUE,
        //     FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION,
        //     &bytesReturned,
        //     &overlapped,
        //     nullptr
        // );
        // convert LPCWSTR to LPCSTR

        // if (!success)
        // {
        //     fprintf(stderr, "Failed to read directory changes: %d\n", GetLastError());
        //     break;
        // }

        // if (GetOverlappedResultEx(&hDir, &overlapped, &bytesReturned, INFINITE, FALSE))
        // {
        //     uint32_t offset = 0;
        //     PFILE_NOTIFY_INFORMATION info;
        //     do
        //     {
        //         info = &buffer[offset];
        //         printf("File changed: %ls\n", info->FileName);
        //         offset += info->NextEntryOffset;
        //     } while (info->NextEntryOffset != 0);
        //
        //     callback(arg);
        //     ResetEvent(overlapped.hEvent);
        // }
        // else
        // {
        //     fprintf(stderr, "Wait for event failed: %x\n", HRESULT_FROM_WIN32(GetLastError()));
        // }
    }

    // CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    pthread_exit(nullptr);
}

void addFileWatcher(LPCSTR path, void *(*callback)(void *), void *arg)
{
    if (fileWatcherThread) return;

    threadArgs.path = path;
    threadArgs.callback = callback;
    threadArgs.arg = arg;

    fileWatcherRunning = true;
    pthread_create(&fileWatcherThread, nullptr, reinterpret_cast<void *(*)(void *)>(fileWatcherInit), &threadArgs);
}

void removeFileWatcher()
{
    if (!fileWatcherRunning) return;

    fileWatcherRunning = false;

    // CancelIoEx(hDir, &overlapped);
    // if (overlapped.hEvent != INVALID_HANDLE_VALUE)
    // {
    //     CloseHandle(overlapped.hEvent);
    //     overlapped.hEvent = INVALID_HANDLE_VALUE;
    // }

    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    pthread_join(fileWatcherThread, nullptr);
}


