// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright (c) 2020 The Base Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"

#include <io.h>
#include <shlobj.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>

#include "absl/random/random.h"
#include "base/files/file_enumerator.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time_util.h"

namespace base {

namespace {

const DWORD kFileShareAll =
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// Returns the Win32 last error code or ERROR_SUCCESS if the last error code is
// ERROR_FILE_NOT_FOUND or ERROR_PATH_NOT_FOUND. This is useful in cases where
// the absence of a file or path is a success condition (e.g., when attempting
// to delete an item in the filesystem).
DWORD ReturnLastErrorOrSuccessOnNotFound() {
  const DWORD error_code = ::GetLastError();
  return (error_code == ERROR_FILE_NOT_FOUND ||
          error_code == ERROR_PATH_NOT_FOUND)
             ? ERROR_SUCCESS
             : error_code;
}

// Deletes all files and directories in a path.
// Returns ERROR_SUCCESS on success or the Windows error code corresponding to
// the first error encountered. ERROR_FILE_NOT_FOUND and ERROR_PATH_NOT_FOUND
// are considered success conditions, and are therefore never returned.
DWORD DeleteFileRecursive(const FilePath& path, const std::string& pattern,
                          bool recursive) {
  FileEnumerator traversal(path, false,
                           FileEnumerator::FILES | FileEnumerator::DIRECTORIES,
                           pattern);
  DWORD result = ERROR_SUCCESS;
  for (FilePath current = traversal.Next(); !current.empty();
       current = traversal.Next()) {
    // Try to clear the read-only bit if we find it.
    FileEnumerator::FileInfo info = traversal.GetInfo();
    if ((info.find_data().dwFileAttributes & FILE_ATTRIBUTE_READONLY) &&
        (recursive || !info.IsDirectory())) {
      ::SetFileAttributes(
          current.value().c_str(),
          info.find_data().dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
    }

    DWORD this_result = ERROR_SUCCESS;
    if (info.IsDirectory()) {
      if (recursive) {
        this_result = DeleteFileRecursive(current, pattern, true);
        DCHECK_NE(static_cast<LONG>(this_result), ERROR_FILE_NOT_FOUND);
        DCHECK_NE(static_cast<LONG>(this_result), ERROR_PATH_NOT_FOUND);
        if (this_result == ERROR_SUCCESS &&
            !::RemoveDirectory(current.value().c_str())) {
          this_result = ReturnLastErrorOrSuccessOnNotFound();
        }
      }
    } else if (!::DeleteFile(current.value().c_str())) {
      this_result = ReturnLastErrorOrSuccessOnNotFound();
    }
    if (result == ERROR_SUCCESS) result = this_result;
  }
  return result;
}

// Appends |mode_char| to |mode| before the optional character set encoding; see
// https://msdn.microsoft.com/library/yeby3zcb.aspx for details.
void AppendModeCharacter(char mode_char, std::string* mode) {
  size_t comma_pos = mode->find(',');
  mode->insert(comma_pos == std::string::npos ? mode->length() : comma_pos, 1,
               mode_char);
}

bool DoCopyFile(const FilePath& from_path, const FilePath& to_path,
                bool fail_if_exists) {
  if (from_path.ReferencesParent() || to_path.ReferencesParent()) return false;

  // NOTE: I suspect we could support longer paths, but that would involve
  // analyzing all our usage of files.
  if (from_path.value().length() >= MAX_PATH ||
      to_path.value().length() >= MAX_PATH) {
    return false;
  }

  // Mitigate the issues caused by loading DLLs on a background thread
  // (http://crbug/973868).
  // SCOPED_MAY_LOAD_LIBRARY_AT_BACKGROUND_PRIORITY();

  // Unlike the posix implementation that copies the file manually and discards
  // the ACL bits, CopyFile() copies the complete SECURITY_DESCRIPTOR and access
  // bits, which is usually not what we want. We can't do much about the
  // SECURITY_DESCRIPTOR but at least remove the read only bit.
  const char* dest = to_path.value().c_str();
  if (!::CopyFile(from_path.value().c_str(), dest, fail_if_exists)) {
    // Copy failed.
    return false;
  }
  DWORD attrs = GetFileAttributes(dest);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  if (attrs & FILE_ATTRIBUTE_READONLY) {
    SetFileAttributes(dest, attrs & ~FILE_ATTRIBUTE_READONLY);
  }
  return true;
}

bool DoCopyDirectory(const FilePath& from_path, const FilePath& to_path,
                     bool recursive, bool fail_if_exists) {
  // NOTE(maruel): Previous version of this function used to call
  // SHFileOperation().  This used to copy the file attributes and extended
  // attributes, OLE structured storage, NTFS file system alternate data
  // streams, SECURITY_DESCRIPTOR. In practice, this is not what we want, we
  // want the containing directory to propagate its SECURITY_DESCRIPTOR.

  // NOTE: I suspect we could support longer paths, but that would involve
  // analyzing all our usage of files.
  if (from_path.value().length() >= MAX_PATH ||
      to_path.value().length() >= MAX_PATH) {
    return false;
  }

  // This function does not properly handle destinations within the source.
  FilePath real_to_path = to_path;
  if (PathExists(real_to_path)) {
    real_to_path = MakeAbsoluteFilePath(real_to_path);
    if (real_to_path.empty()) return false;
  } else {
    real_to_path = MakeAbsoluteFilePath(real_to_path.DirName());
    if (real_to_path.empty()) return false;
  }
  FilePath real_from_path = MakeAbsoluteFilePath(from_path);
  if (real_from_path.empty()) return false;
  if (real_to_path == real_from_path || real_from_path.IsParent(real_to_path))
    return false;

  int traverse_type = FileEnumerator::FILES;
  if (recursive) traverse_type |= FileEnumerator::DIRECTORIES;
  FileEnumerator traversal(from_path, recursive, traverse_type);

  if (!PathExists(from_path)) {
    DLOG(ERROR) << "CopyDirectory() couldn't stat source directory: "
                << from_path.value().c_str();
    return false;
  }

  FilePath current = from_path;
  bool from_is_dir = DirectoryExists(from_path);
  bool success = true;
  FilePath from_path_base = from_path;
  if (recursive && DirectoryExists(to_path)) {
    // If the destination already exists and is a directory, then the
    // top level of source needs to be copied.
    from_path_base = from_path.DirName();
  }

  while (success && !current.empty()) {
    // current is the source path, including from_path, so append
    // the suffix after from_path to to_path to create the target_path.
    FilePath target_path(to_path);
    if (from_path_base != current) {
      if (!from_path_base.AppendRelativePath(current, &target_path)) {
        success = false;
        break;
      }
    }

    if (from_is_dir) {
      if (!DirectoryExists(target_path) &&
          !::CreateDirectory(target_path.value().c_str(), NULL)) {
        DLOG(ERROR) << "CopyDirectory() couldn't create directory: "
                    << target_path.value().c_str();
        success = false;
      }
    } else if (!DoCopyFile(current, target_path, fail_if_exists)) {
      DLOG(ERROR) << "CopyDirectory() couldn't create file: "
                  << target_path.value().c_str();
      success = false;
    }

    current = traversal.Next();
    if (!current.empty()) from_is_dir = traversal.GetInfo().IsDirectory();
  }

  return success;
}

// Returns ERROR_SUCCESS on success, or a Windows error code on failure.
DWORD DoDeleteFile(const FilePath& path, bool recursive) {
  DCHECK(path.value().find('*') == std::string::npos);

  if (path.empty()) return ERROR_SUCCESS;

  if (path.value().length() >= MAX_PATH) return ERROR_BAD_PATHNAME;

  // Report success if the file or path does not exist.
  const DWORD attr = ::GetFileAttributes(path.value().c_str());
  if (attr == INVALID_FILE_ATTRIBUTES)
    return ReturnLastErrorOrSuccessOnNotFound();

  // Clear the read-only bit if it is set.
  if ((attr & FILE_ATTRIBUTE_READONLY) &&
      !::SetFileAttributes(path.value().c_str(),
                           attr & ~FILE_ATTRIBUTE_READONLY)) {
    // It's possible for |path| to be gone now under a race with other deleters.
    return ReturnLastErrorOrSuccessOnNotFound();
  }

  // Perform a simple delete on anything that isn't a directory.
  if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    return ::DeleteFile(path.value().c_str())
               ? ERROR_SUCCESS
               : ReturnLastErrorOrSuccessOnNotFound();
  }

  if (recursive) {
    const DWORD error_code = DeleteFileRecursive(path, "*", true);
    DCHECK_NE(static_cast<LONG>(error_code), ERROR_FILE_NOT_FOUND);
    DCHECK_NE(static_cast<LONG>(error_code), ERROR_PATH_NOT_FOUND);
    if (error_code != ERROR_SUCCESS) return error_code;
  }
  return ::RemoveDirectory(path.value().c_str())
             ? ERROR_SUCCESS
             : ReturnLastErrorOrSuccessOnNotFound();
}

}  // namespace

FilePath MakeAbsoluteFilePath(const FilePath& input) {
  char file_path[MAX_PATH];
  if (!_fullpath(file_path, input.value().c_str(), MAX_PATH)) return FilePath();
  return FilePath(file_path);
}

bool DeleteFile(const FilePath& path, bool recursive) {
  return DoDeleteFile(path, recursive) == ERROR_SUCCESS;
}

bool DeleteFileRecursively(const FilePath& path) {
  return DoDeleteFile(path, /*recursive=*/true) == ERROR_SUCCESS;
}

bool ReplaceFile(const FilePath& from_path, const FilePath& to_path,
                 File::Error* error) {
  // Try a simple move first.  It will only succeed when |to_path| doesn't
  // already exist.
  if (::MoveFile(from_path.value().c_str(), to_path.value().c_str()))
    return true;
  File::Error move_error = File::OSErrorToFileError(GetLastError());

  // Try the full-blown replace if the move fails, as ReplaceFile will only
  // succeed when |to_path| does exist. When writing to a network share, we may
  // not be able to change the ACLs. Ignore ACL errors then
  // (REPLACEFILE_IGNORE_MERGE_ERRORS).
  if (::ReplaceFile(to_path.value().c_str(), from_path.value().c_str(), NULL,
                    REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
    return true;
  }
  // In the case of FILE_ERROR_NOT_FOUND from ReplaceFile, it is likely that
  // |to_path| does not exist. In this case, the more relevant error comes
  // from the call to MoveFile.
  if (error) {
    File::Error replace_error = File::OSErrorToFileError(GetLastError());
    *error = replace_error == File::FILE_ERROR_NOT_FOUND ? move_error
                                                         : replace_error;
  }
  return false;
}

bool CopyDirectory(const FilePath& from_path, const FilePath& to_path,
                   bool recursive) {
  return DoCopyDirectory(from_path, to_path, recursive, false);
}

bool CopyDirectoryExcl(const FilePath& from_path, const FilePath& to_path,
                       bool recursive) {
  return DoCopyDirectory(from_path, to_path, recursive, true);
}

bool PathExists(const FilePath& path) {
  return (GetFileAttributes(path.value().c_str()) != INVALID_FILE_ATTRIBUTES);
}

bool PathIsWritable(const FilePath& path) {
  HANDLE dir =
      CreateFile(path.value().c_str(), FILE_ADD_FILE, kFileShareAll, NULL,
                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

  if (dir == INVALID_HANDLE_VALUE) return false;

  CloseHandle(dir);
  return true;
}

bool DirectoryExists(const FilePath& path) {
  DWORD fileattr = GetFileAttributes(path.value().c_str());
  if (fileattr != INVALID_FILE_ATTRIBUTES)
    return (fileattr & FILE_ATTRIBUTE_DIRECTORY) != 0;
  return false;
}

bool GetTempDir(FilePath* path) {
  char temp_path[MAX_PATH + 1];
  DWORD path_len = ::GetTempPath(MAX_PATH, temp_path);
  if (path_len >= MAX_PATH || path_len <= 0) return false;
  // TODO(evanm): the old behavior of this function was to always strip the
  // trailing slash.  We duplicate this here, but it shouldn't be necessary
  // when everyone is using the appropriate FilePath APIs.
  *path = FilePath(temp_path).StripTrailingSeparators();
  return true;
}

FilePath GetHomeDir() {
  char result[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT,
                                result)) &&
      result[0]) {
    return FilePath(result);
  }

  // Fall back to the temporary directory on failure.
  FilePath temp;
  if (GetTempDir(&temp)) return temp;

  // Last resort.
  return FilePath("C:\\");
}

bool CreateTemporaryFile(FilePath* path) {
  FilePath temp_file;

  if (!GetTempDir(path)) return false;

  if (CreateTemporaryFileInDir(*path, &temp_file)) {
    *path = temp_file;
    return true;
  }

  return false;
}

// On POSIX we have semantics to create and open a temporary file
// atomically.
// TODO(jrg): is there equivalent call to use on Windows instead of
// going 2-step?
FILE* CreateAndOpenTemporaryFileInDir(const FilePath& dir, FilePath* path) {
  if (!CreateTemporaryFileInDir(dir, path)) {
    return NULL;
  }
  // Open file in binary mode, to avoid problems with fwrite. On Windows
  // it replaces \n's with \r\n's, which may surprise you.
  // Reference: http://msdn.microsoft.com/en-us/library/h9t88zwz(VS.71).aspx
  return OpenFile(*path, "wb+");
}

bool CreateTemporaryFileInDir(const FilePath& dir, FilePath* temp_file) {
  // Use GUID instead of ::GetTempFileName() to generate unique file names.
  // "Due to the algorithm used to generate file names, GetTempFileName can
  // perform poorly when creating a large number of files with the same prefix.
  // In such cases, it is recommended that you construct unique file names based
  // on GUIDs."
  // https://msdn.microsoft.com/library/windows/desktop/aa364991.aspx

  FilePath temp_name;
  bool create_file_success = false;

  // Although it is nearly impossible to get a duplicate name with GUID, we
  // still use a loop here in case it happens.
  for (int i = 0; i < 100; ++i) {
    temp_name = dir.Append(GenerateGUID() + ".tmp");
    File file(temp_name,
              File::FLAG_CREATE | File::FLAG_READ | File::FLAG_WRITE);
    if (file.IsValid()) {
      file.Close();
      create_file_success = true;
      break;
    }
  }

  if (!create_file_success) {
    DPLOG(WARNING) << "Failed to get temporary file name in " << dir.value();
    return false;
  }

  char long_temp_name[MAX_PATH + 1];
  DWORD long_name_len =
      GetLongPathName(temp_name.value().c_str(), long_temp_name, MAX_PATH);
  if (long_name_len > MAX_PATH || long_name_len == 0) {
    // GetLongPathName() failed, but we still have a temporary file.
    *temp_file = std::move(temp_name);
    return true;
  }

  absl::string_view long_temp_name_str(long_temp_name, long_name_len);
  *temp_file = FilePath(long_temp_name_str);
  return true;
}

bool CreateTemporaryDirInDir(const FilePath& base_dir,
                             const std::string& prefix, FilePath* new_dir) {
  FilePath path_to_create;

  absl::BitGen bitgen;
  for (int count = 0; count < 50; ++count) {
    // Try create a new temporary directory with random generated name. If
    // the one exists, keep trying another path name until we reach some limit.
    std::string new_dir_name;
    new_dir_name.assign(prefix);
    new_dir_name.append(NumberToString(GetCurrentProcId()));
    new_dir_name.push_back('_');
    new_dir_name.append(NumberToString(absl::Uniform<int32_t>(
        bitgen, 0, std::numeric_limits<int32_t>::max())));

    path_to_create = base_dir.Append(new_dir_name);
    if (::CreateDirectory(path_to_create.value().c_str(), NULL)) {
      *new_dir = path_to_create;
      return true;
    }
  }

  return false;
}

bool CreateNewTempDirectory(const std::string& prefix,
                            FilePath* new_temp_path) {
  FilePath system_temp_dir;
  if (!GetTempDir(&system_temp_dir)) return false;

  return CreateTemporaryDirInDir(system_temp_dir, prefix, new_temp_path);
}

bool CreateDirectoryAndGetError(const FilePath& full_path, File::Error* error) {
  // If the path exists, we've succeeded if it's a directory, failed otherwise.
  const char* const full_path_str = full_path.value().c_str();
  const DWORD fileattr = ::GetFileAttributes(full_path_str);
  if (fileattr != INVALID_FILE_ATTRIBUTES) {
    if ((fileattr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      DVLOG(1) << "CreateDirectory(" << full_path_str << "), "
               << "directory already exists.";
      return true;
    }
    DLOG(WARNING) << "CreateDirectory(" << full_path_str << "), "
                  << "conflicts with existing file.";
    if (error) *error = File::FILE_ERROR_NOT_A_DIRECTORY;
    ::SetLastError(ERROR_FILE_EXISTS);
    return false;
  }

  // Invariant:  Path does not exist as file or directory.

  // Attempt to create the parent recursively.  This will immediately return
  // true if it already exists, otherwise will create all required parent
  // directories starting with the highest-level missing parent.
  FilePath parent_path(full_path.DirName());
  if (parent_path.value() == full_path.value()) {
    if (error) *error = File::FILE_ERROR_NOT_FOUND;
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    return false;
  }
  if (!CreateDirectoryAndGetError(parent_path, error)) {
    DLOG(WARNING) << "Failed to create one of the parent directories.";
    DCHECK(!error || *error != File::FILE_OK);
    return false;
  }

  if (::CreateDirectory(full_path_str, NULL)) return true;

  const DWORD error_code = ::GetLastError();
  if (error_code == ERROR_ALREADY_EXISTS && DirectoryExists(full_path)) {
    // This error code ERROR_ALREADY_EXISTS doesn't indicate whether we were
    // racing with someone creating the same directory, or a file with the same
    // path.  If DirectoryExists() returns true, we lost the race to create the
    // same directory.
    return true;
  }
  if (error) *error = File::OSErrorToFileError(error_code);
  ::SetLastError(error_code);
  DPLOG(WARNING) << "Failed to create directory " << full_path_str;
  return false;
}

// TODO(rkc): Work out if we want to handle NTFS junctions here or not, handle
// them if we do decide to.
bool IsLink(const FilePath& file_path) { return false; }

bool GetFileInfo(const FilePath& file_path, File::Info* results) {
  WIN32_FILE_ATTRIBUTE_DATA attr;
  if (!GetFileAttributesEx(file_path.value().c_str(), GetFileExInfoStandard,
                           &attr)) {
    return false;
  }

  ULARGE_INTEGER size;
  size.HighPart = attr.nFileSizeHigh;
  size.LowPart = attr.nFileSizeLow;
  results->size = size.QuadPart;

  results->is_directory =
      (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  results->last_modified = FromFileTime(attr.ftLastWriteTime);
  results->last_accessed = FromFileTime(attr.ftLastAccessTime);
  results->creation_time = FromFileTime(attr.ftCreationTime);

  return true;
}

FILE* OpenFile(const FilePath& filename, const char* mode) {
  // 'N' is unconditionally added below, so be sure there is not one already
  // present before a comma in |mode|.
  DCHECK(
      strchr(mode, 'N') == nullptr ||
      (strchr(mode, ',') != nullptr && strchr(mode, 'N') > strchr(mode, ',')));
  std::string mode_str = mode;
  AppendModeCharacter('N', &mode_str);
  return _fsopen(filename.value().c_str(), mode_str.c_str(), _SH_DENYNO);
}

FILE* FileToFILE(File file, const char* mode) {
  if (!file.IsValid()) return NULL;
  int fd =
      _open_osfhandle(reinterpret_cast<intptr_t>(file.GetPlatformFile()), 0);
  if (fd < 0) return NULL;
  file.TakePlatformFile();
  FILE* stream = _fdopen(fd, mode);
  if (!stream) _close(fd);
  return stream;
}

int ReadFile(const FilePath& filename, char* data, int max_size) {
  win::ScopedHandle file(CreateFile(filename.value().c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                                    NULL));
  if (!file.IsValid()) return -1;

  DWORD read;
  if (::ReadFile(file.Get(), data, max_size, &read, NULL)) return read;

  return -1;
}

int WriteFile(const FilePath& filename, const char* data, int size) {
  win::ScopedHandle file(CreateFile(filename.value().c_str(), GENERIC_WRITE, 0,
                                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    NULL));
  if (!file.IsValid()) {
    DPLOG(WARNING) << "CreateFile failed for path " << filename.value();
    return -1;
  }

  DWORD written;
  BOOL result = ::WriteFile(file.Get(), data, size, &written, NULL);
  if (result && static_cast<int>(written) == size) return written;

  if (!result) {
    // WriteFile failed.
    DPLOG(WARNING) << "writing file " << filename.value() << " failed";
  } else {
    // Didn't write all the bytes.
    DLOG(WARNING) << "wrote" << written << " bytes to " << filename.value()
                  << " expected " << size;
  }
  return -1;
}

bool AppendToFile(const FilePath& filename, const char* data, int size) {
  win::ScopedHandle file(CreateFile(filename.value().c_str(), FILE_APPEND_DATA,
                                    0, NULL, OPEN_EXISTING, 0, NULL));
  if (!file.IsValid()) {
    VPLOG(1) << "CreateFile failed for path " << filename.value();
    return false;
  }

  DWORD written;
  BOOL result = ::WriteFile(file.Get(), data, size, &written, NULL);
  if (result && static_cast<int>(written) == size) return true;

  if (!result) {
    // WriteFile failed.
    VPLOG(1) << "Writing file " << filename.value() << " failed";
  } else {
    // Didn't write all the bytes.
    VPLOG(1) << "Only wrote " << written << " out of " << size << " byte(s) to "
             << filename.value();
  }
  return false;
}

bool GetCurrentDirectory(FilePath* dir) {
  char system_buffer[MAX_PATH];
  system_buffer[0] = 0;
  DWORD len = ::GetCurrentDirectory(MAX_PATH, system_buffer);
  if (len == 0 || len > MAX_PATH) return false;
  *dir = FilePath(absl::string_view(system_buffer));
  return true;
}

bool SetCurrentDirectory(const FilePath& directory) {
  return ::SetCurrentDirectory(directory.value().c_str()) != 0;
}

bool CopyFile(const FilePath& from_path, const FilePath& to_path) {
  return DoCopyFile(from_path, to_path, false);
}

bool SetNonBlocking(int fd) {
  unsigned long nonblocking = 1;
  if (ioctlsocket(fd, FIONBIO, &nonblocking) == 0) return true;
  return false;
}

// -----------------------------------------------------------------------------

namespace internal {

bool MoveUnsafe(const FilePath& from_path, const FilePath& to_path) {
  // NOTE: I suspect we could support longer paths, but that would involve
  // analyzing all our usage of files.
  if (from_path.value().length() >= MAX_PATH ||
      to_path.value().length() >= MAX_PATH) {
    return false;
  }
  if (MoveFileEx(from_path.value().c_str(), to_path.value().c_str(),
                 MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING) != 0)
    return true;

  // Keep the last error value from MoveFileEx around in case the below
  // fails.
  bool ret = false;
  DWORD last_error = ::GetLastError();

  if (DirectoryExists(from_path)) {
    // MoveFileEx fails if moving directory across volumes. We will simulate
    // the move by using Copy and Delete. Ideally we could check whether
    // from_path and to_path are indeed in different volumes.
    ret = internal::CopyAndDeleteDirectory(from_path, to_path);
  }

  if (!ret) {
    // Leave a clue about what went wrong so that it can be (at least) picked
    // up by a PLOG entry.
    ::SetLastError(last_error);
  }

  return ret;
}

bool CopyAndDeleteDirectory(const FilePath& from_path,
                            const FilePath& to_path) {
  if (CopyDirectory(from_path, to_path, true)) {
    if (DeleteFileRecursively(from_path)) return true;

    // Like Move, this function is not transactional, so we just
    // leave the copied bits behind if deleting from_path fails.
    // If to_path exists previously then we have already overwritten
    // it by now, we don't get better off by deleting the new bits.
  }
  return false;
}

}  // namespace internal
}  // namespace base