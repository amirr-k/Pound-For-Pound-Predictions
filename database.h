#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>

struct SymbolRow {
  std::string kind;
  std::string name;
  std::string qualname;
  std::string path;
  int start_line;
  int end_line;
  std::string snippet;
};

struct FileRow {
  int id;
  std::string path;
  std::string content_hash;
  int64_t mtime_ns;
  int64_t size_bytes;
};

sqlite3* db_open(const std::string& path);
void db_close(sqlite3* db);
void db_init_schema(sqlite3* db);
int db_upsert_file(sqlite3* db, const std::string& path, const std::string& hash,
                   int64_t mtime_ns, int64_t size_bytes);
FileRow* db_get_file(sqlite3* db, const std::string& path);
void db_replace_symbols(sqlite3* db, int file_id, const std::vector<SymbolRow>& symbols);
std::vector<SymbolRow> db_search(sqlite3* db, const std::string& query, int limit);
