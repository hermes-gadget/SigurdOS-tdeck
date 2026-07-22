#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace sigurdos::test {

class UniqueTempFile {
 public:
  explicit UniqueTempFile(std::string path) : path_(std::move(path)) {}
  operator const char*() const { return path_.c_str(); }
  const char* c_str() const { return path_.c_str(); }

 private:
  std::string path_;
};

class UniqueTempDir {
 public:
  UniqueTempDir() {
    char pattern[] = "/tmp/sigurdos-test-XXXXXX";
    char* created = mkdtemp(pattern);
    if (!created) throw std::runtime_error("mkdtemp failed");
    path_ = created;
  }

  ~UniqueTempDir() { rmdir(path_.c_str()); }

  UniqueTempFile file(const char* leaf) const {
    return UniqueTempFile(path_ + "/" + leaf);
  }

  UniqueTempDir(const UniqueTempDir&) = delete;
  UniqueTempDir& operator=(const UniqueTempDir&) = delete;

 private:
  std::string path_;
};

inline const UniqueTempDir& processTempDir() {
  static const UniqueTempDir directory;
  return directory;
}

}  // namespace sigurdos::test
