// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright (c) 2020 The Base Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"

#import <Foundation/Foundation.h>

#include <copyfile.h>

#include "base/mac/foundation_util.h"

namespace base {

bool CopyFile(const FilePath& from_path, const FilePath& to_path) {
  if (from_path.ReferencesParent() || to_path.ReferencesParent()) return false;
  return (copyfile(from_path.value().c_str(), to_path.value().c_str(), NULL, COPYFILE_DATA) == 0);
}

bool GetTempDir(FilePath* path) {
  // If we didn't find it, fall back to the native function.
  NSString* tmp = NSTemporaryDirectory();
  if (tmp == nil) return false;
  *path = mac::NSStringToFilePath(tmp);
  return true;
}

FilePath GetHomeDir() {
  NSString* tmp = NSHomeDirectory();
  if (tmp != nil) {
    FilePath mac_home_dir = mac::NSStringToFilePath(tmp);
    if (!mac_home_dir.empty()) return mac_home_dir;
  }

  // Fall back on temp dir if no home directory is defined.
  FilePath rv;
  if (GetTempDir(&rv)) return rv;

  // Last resort.
  return FilePath("/tmp");
}

}  // namespace base
