#include "workspace.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "xxhash.h"

namespace fs = std::filesystem;

namespace {

int64_t file_time_to_unix_ns(const fs::file_time_type& ft) {
  using namespace std::chrono;
  const auto sys_now = system_clock::now();
  const auto fs_now = fs::file_time_type::clock::now();
  const auto sctp =
      time_point_cast<system_clock::duration>(sys_now + (ft - fs_now));
  return duration_cast<nanoseconds>(sctp.time_since_epoch()).count();
}

bool indexable_ext(const fs::path& p) {
  std::string e = p.extension().string();
  for (char& c : e) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c" || e == ".h" ||
         e == ".hpp" || e == ".hxx";
}

bool is_relative_hidden(const fs::path& root, const fs::path& p) {
  std::error_code ec;
  const fs::path rel = fs::relative(p, root, ec);
  if (ec) {
    return true;
  }
  for (const fs::path& part : rel) {
    const std::string name = part.string();
    if (!name.empty() && name != "." && name != ".." && name.front() == '.') {
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<FileEntry> ws_crawl(const std::string& repo_root) {
  std::vector<FileEntry> out;
  std::error_code ec;
  const fs::path root = fs::canonical(repo_root, ec);
  if (ec) {
    return out;
  }

  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    return out;
  }

  for (; it != fs::recursive_directory_iterator(); ++it) {
    const fs::directory_entry& ent = *it;
    const fs::path& p = ent.path();

    if (is_relative_hidden(root, p)) {
      std::error_code st_ec;
      if (ent.is_directory(st_ec)) {
        it.disable_recursion_pending();
      }
      continue;
    }

    std::error_code reg_ec;
    if (!ent.is_regular_file(reg_ec)) {
      continue;
    }
    if (!indexable_ext(p)) {
      continue;
    }

    std::error_code rel_ec;
    const fs::path rel = fs::relative(p, root, rel_ec);
    if (rel_ec) {
      continue;
    }

    std::error_code sz_ec;
    const auto sz = fs::file_size(p, sz_ec);
    if (sz_ec) {
      continue;
    }

    std::error_code mt_ec;
    const auto mt = fs::last_write_time(p, mt_ec);
    if (mt_ec) {
      continue;
    }

    FileEntry fe;
    fe.path = rel.generic_string();
    fe.content_hash.clear();
    fe.mtime_ns = file_time_to_unix_ns(mt);
    fe.size_bytes = static_cast<int64_t>(sz);
    out.push_back(std::move(fe));
  }

  return out;
}

bool ws_should_index(const FileEntry& entry, const std::string& stored_hash) {
  if (stored_hash.empty()) {
    return true;
  }
  return entry.content_hash != stored_hash;
}

std::string ws_hash_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return "";
  }

  const std::streamoff end = in.tellg();
  if (end < 0) {
    return "";
  }
  const auto len = static_cast<std::size_t>(end);
  in.seekg(0, std::ios::beg);
  std::vector<char> buf(len);
  if (len > 0 && !in.read(buf.data(), static_cast<std::streamsize>(len))) {
    return "";
  }

  const XXH64_hash_t h = XXH64(buf.data(), len, 0);
  std::ostringstream oss;
  oss << std::hex << std::nouppercase << std::setw(16) << std::setfill('0')
      << static_cast<unsigned long long>(h);
  return oss.str();
}
