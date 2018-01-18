
#include "stdafx.h"
#include "file_change_watcher.h"
#include "string_utilities.h"

// on windows, we need to compare directories icase

FileChangeWatcher::FileChangeWatcher(FileWatchCallback callback, void * argument) 
  : running_(false) 
  , callback_function_(callback)
  , callback_argument_(argument) {

  InitializeCriticalSectionAndSpinCount(&critical_section_, 0x00000400);
  update_watch_list_handle_ = CreateEvent(0, TRUE, FALSE, 0);
}

FileChangeWatcher::~FileChangeWatcher() {
  CloseHandle(update_watch_list_handle_);
  DeleteCriticalSection(&critical_section_);
}

void FileChangeWatcher::WatchDirectory(const std::string &directory) {

  // FIXME: it might be better to resolve the path. in windows
  // I think either findfirstfile or getlongpathname/getshortpathname
  // will do resolution 

  std::string local_string = directory;

  // scrub: remove trailing separator, if any
  while(StringUtilities::EndsWith(local_string, "\\")
     || StringUtilities::EndsWith(local_string, "/")) {
    local_string = local_string.substr(0, local_string.length() - 1);
  }

  EnterCriticalSection(&critical_section_);

  // don't double up
  for (auto entry : watched_directories_) {
    if (!StringUtilities::ICaseCompare(local_string, entry)) return;
  }
  watched_directories_.push_back(local_string);
  LeaveCriticalSection(&critical_section_);

  // notify the watch thread
  SetEvent(update_watch_list_handle_);

}

/** remove a watched directory */
void FileChangeWatcher::UnwatchDirectory(const std::string &directory) {

  std::vector< std::string > tmp;
  EnterCriticalSection(&critical_section_);

  for (auto entry : watched_directories_) {
    if (StringUtilities::ICaseCompare(directory, entry)) tmp.push_back(entry);
  }
  watched_directories_ = tmp;
  LeaveCriticalSection(&critical_section_);
  SetEvent(update_watch_list_handle_);

}

void FileChangeWatcher::StartWatch() {
  if (running_) {
    DebugOut("ERROR: already watching\n");
    return;
  }
  uintptr_t thread_ptr = _beginthreadex(0, 0, StartThread, this, 0, 0);
}

void FileChangeWatcher::Shutdown() {
  if (running_) {
    running_ = false;

    // ... wait ...
  }
}

void FileChangeWatcher::NotifyDirectoryChanges(const std::vector<std::string> &directory_list, FILETIME update_time) {

  // we have a list of directories that have changes. we want
  // to convert this into a list of files. to do that, we use   <-- nicely formatted text block
  // the update time as a comparison against last modify time

  std::vector<std::string> files;
  char path[MAX_PATH];
  WIN32_FIND_DATAA find_data_info;

  for (auto directory : directory_list) {
    strcpy_s(path, directory.c_str());
    strcat_s(path, "\\*");

    HANDLE find_handle = FindFirstFileA(path, &find_data_info);
    if (find_handle && find_handle != INVALID_HANDLE_VALUE) {
      do {
        if (!(find_data_info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))
            && (1 == CompareFileTime(&(find_data_info.ftLastWriteTime), &update_time))){
            std::string match = directory;
            match += "\\";
            match += find_data_info.cFileName;
            files.push_back(match);
        }
      } while (FindNextFileA(find_handle, &find_data_info));
    }

  }

  // temp
  if (callback_function_ && files.size()) {
    callback_function_(callback_argument_, files);
  }

}

uint32_t FileChangeWatcher::InstanceStartThread() {

  SYSTEMTIME system_time;
  GetSystemTime(&system_time);
  FILETIME last_update;
  SystemTimeToFileTime(&system_time, &last_update);

  running_ = true;
  while (running_) {

    // watch all directories in the list. because the list may change
    // on another thread, we want to keep a local mapping.

    std::vector < std::string > directories;
    EnterCriticalSection(&critical_section_);

    // just in case, for first time
    ResetEvent(update_watch_list_handle_);
    for (auto entry : watched_directories_) {
      directories.push_back(entry);
    }
    LeaveCriticalSection(&critical_section_);

    int count = directories.size();
    std::vector < HANDLE > handles;
    for (auto entry : directories) {
      HANDLE handle = FindFirstChangeNotificationA(entry.c_str(), FALSE, FILE_WATCH_EVENT_MASK);
      if (!handle || handle == INVALID_HANDLE_VALUE) {
        DebugOut("WARNING: bad handle @ %s\n", entry.c_str());
      }
      handles.push_back(handle);
    }

    // extra handle for updates to list
    handles.push_back(update_watch_list_handle_);

    // debouncing events
    std::vector < std::string > change_list;

    bool looping = true;

    while (looping && running_) {

      DWORD timeout = change_list.size() ? FILE_WATCH_LOOP_DEBOUNCE_TIMEOUT : FILE_WATCH_LOOP_NORMAL_TIMEOUT;
      DWORD wait_result = WaitForMultipleObjects(count + 1, &handles[0], FALSE, timeout);

      if (wait_result == WAIT_OBJECT_0 + count) {
        ResetEvent(handles[wait_result - WAIT_OBJECT_0]);
        DebugOut(" ** Directory list change, restart\n");
        looping = false;
        break;
      }

      else if (wait_result >= WAIT_OBJECT_0 && wait_result < WAIT_OBJECT_0 + count) {
        int index = wait_result - WAIT_OBJECT_0;
        DebugOut("(directory change: %s)\n", directories[index].c_str());
        bool found = false;
        for (auto entry : change_list) {
          found = found || !entry.compare(directories[index]);
        }
        if(!found) change_list.push_back(directories[index]);
        FindNextChangeNotification(handles[index]);
      }

      else if (wait_result == WAIT_TIMEOUT) {
        if (change_list.size()) {
          NotifyDirectoryChanges(change_list, last_update);

          GetSystemTime(&system_time);
          SystemTimeToFileTime(&system_time, &last_update);

          change_list.clear();
        }
      }

      else {
        DWORD err = GetLastError();
        DebugOut("Watch failed with error %d\n", err);
        break; // ?
      }
    }

    // close created handles (not the last one)
    for (int i = 0; i < count; i++) {
      FindCloseChangeNotification(handles[i]);
      // no // CloseHandle(handles[i]);
    }

  }

  return 0;
}

uint32_t FileChangeWatcher::StartThread(void *data) {
  FileChangeWatcher *instance = reinterpret_cast<FileChangeWatcher*>(data);
  return instance->InstanceStartThread();
}

