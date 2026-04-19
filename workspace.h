#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct FileEntry {
  std::string path;
  std::string content_hash;
  int64_t mtime_ns;
  int64_t size_bytes;
};

std::vector<FileEntry> ws_crawl(const std::string& repo_root);
bool ws_should_index(const FileEntry& entry, const std::string& stored_hash);
std::string ws_hash_file(const std::string& path);
