#include "database.h"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iostream>

namespace {

void fail_sqlite(sqlite3* db, const char* context) {
  const char* msg = db ? sqlite3_errmsg(db) : "unknown error";
  std::cerr << "codewatch db: " << context << ": " << msg << '\n';
}

bool exec_sql(sqlite3* db, const char* sql, const char* context) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::cerr << "codewatch db: " << context << ": " << (err ? err : sqlite3_errmsg(db))
              << '\n';
    sqlite3_free(err);
    return false;
  }
  return true;
}

std::string trim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

}  // namespace

sqlite3* db_open(const std::string& path) {
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(
      path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (rc != SQLITE_OK || db == nullptr) {
    std::cerr << "codewatch db: sqlite3_open_v2 failed for '" << path << "': "
              << sqlite3_errstr(rc) << '\n';
    if (db) {
      sqlite3_close(db);
    }
    std::exit(1);
  }

  static const char* kPragmas =
      "PRAGMA foreign_keys = ON;"
      "PRAGMA journal_mode = WAL;"
      "PRAGMA synchronous = NORMAL;"
      "PRAGMA temp_store = MEMORY;";
  if (!exec_sql(db, kPragmas, "db_open pragmas")) {
    sqlite3_close(db);
    std::exit(1);
  }

  return db;
}

void db_close(sqlite3* db) {
  if (db == nullptr) {
    return;
  }
  const int rc = sqlite3_close(db);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_close");
  }
}

void db_init_schema(sqlite3* db) {
  static const char* kSql =
      "CREATE TABLE IF NOT EXISTS files ("
      "  id INTEGER PRIMARY KEY,"
      "  path TEXT UNIQUE NOT NULL,"
      "  content_hash TEXT NOT NULL,"
      "  mtime_ns INTEGER NOT NULL,"
      "  size_bytes INTEGER NOT NULL,"
      "  indexed_at_unix INTEGER NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS symbols ("
      "  id INTEGER PRIMARY KEY,"
      "  file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
      "  kind TEXT NOT NULL,"
      "  name TEXT NOT NULL,"
      "  qualname TEXT NOT NULL,"
      "  start_line INTEGER NOT NULL,"
      "  end_line INTEGER NOT NULL,"
      "  snippet TEXT NOT NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_symbols_name ON symbols(name);"
      "CREATE INDEX IF NOT EXISTS idx_symbols_qualname ON symbols(qualname);"
      "CREATE INDEX IF NOT EXISTS idx_symbols_file_id ON symbols(file_id);";

  if (!exec_sql(db, kSql, "db_init_schema")) {
    std::exit(1);
  }
}

int db_upsert_file(sqlite3* db, const std::string& path, const std::string& hash,
                   int64_t mtime_ns, int64_t size_bytes) {
  const char* kSql =
      "INSERT OR REPLACE INTO files (path, content_hash, mtime_ns, size_bytes, indexed_at_unix) "
      "VALUES (?, ?, ?, ?, ?);";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_upsert_file prepare");
    std::exit(1);
  }

  const sqlite3_int64 now = static_cast<sqlite3_int64>(std::time(nullptr));
  sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(mtime_ns));
  sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(size_bytes));
  sqlite3_bind_int64(stmt, 5, now);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    fail_sqlite(db, "db_upsert_file step");
    std::exit(1);
  }

  const char* kSelect = "SELECT id FROM files WHERE path = ?;";
  rc = sqlite3_prepare_v2(db, kSelect, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_upsert_file select prepare");
    std::exit(1);
  }
  sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    fail_sqlite(db, "db_upsert_file select step");
    sqlite3_finalize(stmt);
    std::exit(1);
  }
  const int id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return id;
}

FileRow* db_get_file(sqlite3* db, const std::string& path) {
  const char* kSql =
      "SELECT id, path, content_hash, mtime_ns, size_bytes FROM files WHERE path = ?;";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_get_file prepare");
    std::exit(1);
  }

  sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return nullptr;
  }
  if (rc != SQLITE_ROW) {
    fail_sqlite(db, "db_get_file step");
    sqlite3_finalize(stmt);
    std::exit(1);
  }

  auto* row = new FileRow();
  row->id = sqlite3_column_int(stmt, 0);
  if (const unsigned char* t = sqlite3_column_text(stmt, 1)) {
    row->path = reinterpret_cast<const char*>(t);
  }
  if (const unsigned char* t = sqlite3_column_text(stmt, 2)) {
    row->content_hash = reinterpret_cast<const char*>(t);
  }
  row->mtime_ns = sqlite3_column_int64(stmt, 3);
  row->size_bytes = sqlite3_column_int64(stmt, 4);
  sqlite3_finalize(stmt);
  return row;
}

void db_replace_symbols(sqlite3* db, int file_id, const std::vector<SymbolRow>& symbols) {
  if (!exec_sql(db, "BEGIN IMMEDIATE;", "db_replace_symbols begin")) {
    std::exit(1);
  }

  const char* kDelete = "DELETE FROM symbols WHERE file_id = ?;";
  sqlite3_stmt* del = nullptr;
  int rc = sqlite3_prepare_v2(db, kDelete, -1, &del, nullptr);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_replace_symbols delete prepare");
    exec_sql(db, "ROLLBACK;", "db_replace_symbols rollback");
    std::exit(1);
  }
  sqlite3_bind_int(del, 1, file_id);
  rc = sqlite3_step(del);
  sqlite3_finalize(del);
  if (rc != SQLITE_DONE) {
    fail_sqlite(db, "db_replace_symbols delete step");
    exec_sql(db, "ROLLBACK;", "db_replace_symbols rollback");
    std::exit(1);
  }

  const char* kInsert =
      "INSERT INTO symbols (file_id, kind, name, qualname, start_line, end_line, snippet) "
      "VALUES (?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt* ins = nullptr;
  rc = sqlite3_prepare_v2(db, kInsert, -1, &ins, nullptr);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_replace_symbols insert prepare");
    exec_sql(db, "ROLLBACK;", "db_replace_symbols rollback");
    std::exit(1);
  }

  for (const SymbolRow& s : symbols) {
    sqlite3_reset(ins);
    sqlite3_clear_bindings(ins);
    sqlite3_bind_int(ins, 1, file_id);
    sqlite3_bind_text(ins, 2, s.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, s.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, s.qualname.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 5, s.start_line);
    sqlite3_bind_int(ins, 6, s.end_line);
    sqlite3_bind_text(ins, 7, s.snippet.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(ins);
    if (rc != SQLITE_DONE) {
      fail_sqlite(db, "db_replace_symbols insert step");
      sqlite3_finalize(ins);
      exec_sql(db, "ROLLBACK;", "db_replace_symbols rollback");
      std::exit(1);
    }
  }
  sqlite3_finalize(ins);

  if (!exec_sql(db, "COMMIT;", "db_replace_symbols commit")) {
    exec_sql(db, "ROLLBACK;", "db_replace_symbols rollback");
    std::exit(1);
  }
}

std::vector<SymbolRow> db_search(sqlite3* db, const std::string& query, int limit) {
  std::vector<SymbolRow> out;
  const std::string q = trim_copy(query);
  if (q.empty() || limit <= 0) {
    return out;
  }

  const std::string like = "%" + q + "%";

  static const char* kSql =
      "SELECT s.kind, s.name, s.qualname, f.path, s.start_line, s.end_line, s.snippet "
      "FROM symbols AS s "
      "JOIN files AS f ON f.id = s.file_id "
      "WHERE s.name LIKE ?6 OR s.qualname LIKE ?7 OR s.snippet LIKE ?8 "
      "ORDER BY "
      "  CASE "
      "    WHEN s.name = ?1 THEN 300 "
      "    WHEN s.qualname = ?2 THEN 260 "
      "    WHEN s.name LIKE ?3 THEN 220 "
      "    WHEN s.qualname LIKE ?4 THEN 180 "
      "    WHEN s.snippet LIKE ?5 THEN 100 "
      "    ELSE 0 "
      "  END DESC, "
      "  f.path ASC, "
      "  s.start_line ASC "
      "LIMIT ?9;";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    fail_sqlite(db, "db_search prepare");
    std::exit(1);
  }

  sqlite3_bind_text(stmt, 1, q.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, q.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, like.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, like.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, like.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, like.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, like.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, like.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 9, limit);

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    SymbolRow row;
    if (const unsigned char* t = sqlite3_column_text(stmt, 0)) {
      row.kind = reinterpret_cast<const char*>(t);
    }
    if (const unsigned char* t = sqlite3_column_text(stmt, 1)) {
      row.name = reinterpret_cast<const char*>(t);
    }
    if (const unsigned char* t = sqlite3_column_text(stmt, 2)) {
      row.qualname = reinterpret_cast<const char*>(t);
    }
    if (const unsigned char* t = sqlite3_column_text(stmt, 3)) {
      row.path = reinterpret_cast<const char*>(t);
    }
    row.start_line = sqlite3_column_int(stmt, 4);
    row.end_line = sqlite3_column_int(stmt, 5);
    if (const unsigned char* t = sqlite3_column_text(stmt, 6)) {
      row.snippet = reinterpret_cast<const char*>(t);
    }
    out.push_back(std::move(row));
  }
  if (rc != SQLITE_DONE) {
    fail_sqlite(db, "db_search step");
    sqlite3_finalize(stmt);
    std::exit(1);
  }
  sqlite3_finalize(stmt);
  return out;
}
