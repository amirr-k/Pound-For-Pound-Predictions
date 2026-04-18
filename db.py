from __future__ import annotations

import sqlite3
import time
from pathlib import Path
from typing import Iterable, Mapping


def connect(db_path: str | Path) -> sqlite3.Connection:
    # open sqlite db and always return rows like dicts (sqlite3.Row)
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row

    # pragmas are tuned for local cli speed without going full unsafe mode
    # foreign keys matter because symbols.file_id should die with files.id
    # wal helps read/query while writes happen during indexing
    # normal sync is a pretty normal tradeoff for local tools
    # temp in memory avoids some disk churn on heavier sql ops
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.execute("PRAGMA journal_mode = WAL;")
    conn.execute("PRAGMA synchronous = NORMAL;")
    conn.execute("PRAGMA temp_store = MEMORY;")
    return conn


def init_schema(conn: sqlite3.Connection) -> None:
    # one place for schema so index/query modules stay tiny and focused
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY,
            path TEXT UNIQUE NOT NULL,
            content_hash TEXT NOT NULL,
            mtime_ns INTEGER NOT NULL,
            size_bytes INTEGER NOT NULL,
            last_indexed_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS symbols (
            id INTEGER PRIMARY KEY,
            file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
            kind TEXT NOT NULL,
            name TEXT NOT NULL,
            qualname TEXT NOT NULL,
            start_line INTEGER NOT NULL,
            end_line INTEGER NOT NULL,
            source TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_symbols_name ON symbols(name);
        CREATE INDEX IF NOT EXISTS idx_symbols_qualname ON symbols(qualname);
        CREATE INDEX IF NOT EXISTS idx_symbols_file_id ON symbols(file_id);
        """
    )


def upsert_file(
    conn: sqlite3.Connection,
    *,
    path: str,
    content_hash: str,
    mtime_ns: int,
    size_bytes: int,
) -> int:
    # file row is our incremental indexing anchor
    # same path -> update hash/stats, new path -> insert row
    now_ts = int(time.time())
    conn.execute(
        """
        INSERT INTO files (path, content_hash, mtime_ns, size_bytes, last_indexed_at)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            content_hash = excluded.content_hash,
            mtime_ns = excluded.mtime_ns,
            size_bytes = excluded.size_bytes,
            last_indexed_at = excluded.last_indexed_at
        """,
        (path, content_hash, mtime_ns, size_bytes, now_ts),
    )
    row = conn.execute("SELECT id FROM files WHERE path = ?", (path,)).fetchone()
    if row is None:
        # shouldn't happen unless something weird interrupted writes
        raise RuntimeError(f"Failed to load file id for path: {path}")
    return int(row["id"])


def get_file_by_path(conn: sqlite3.Connection, path: str) -> sqlite3.Row | None:
    # tiny lookup used by index step to decide skip vs reparse
    return conn.execute(
        """
        SELECT id, path, content_hash, mtime_ns, size_bytes, last_indexed_at
        FROM files
        WHERE path = ?
        """,
        (path,),
    ).fetchone()


def replace_symbols_for_file(
    conn: sqlite3.Connection,
    *,
    file_id: int,
    symbols: Iterable[Mapping[str, object]],
) -> int:
    # for changed files we blow away old symbol rows first
    # then insert a fresh snapshot from current ast parse
    conn.execute("DELETE FROM symbols WHERE file_id = ?", (file_id,))

    # normalize everything to db-safe primitive types
    payload = [
        (
            file_id,
            str(sym["kind"]),
            str(sym["name"]),
            str(sym["qualname"]),
            int(sym["start_line"]),
            int(sym["end_line"]),
            str(sym["source"]),
        )
        for sym in symbols
    ]
    if not payload:
        # valid case: file changed but ended up with no extractable symbols
        return 0

    conn.executemany(
        """
        INSERT INTO symbols (
            file_id, kind, name, qualname, start_line, end_line, source
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        payload,
    )
    return len(payload)


def search_symbols(
    conn: sqlite3.Connection, text: str, *, limit: int = 20
) -> list[sqlite3.Row]:
    # v1 search is intentionally simple: basic like matching + lightweight score
    # no fts, no embedding, no fancy ranking yet
    q = text.strip()
    if not q:
        return []

    like = f"%{q}%"
    # score favors exact names first, then partial names, then raw source hits
    # this keeps obvious symbol matches near top with almost no extra logic
    return list(
        conn.execute(
            """
            SELECT
                s.kind,
                s.name,
                s.qualname,
                f.path,
                s.start_line,
                s.end_line,
                s.source,
                CASE
                    WHEN s.name = ? THEN 300
                    WHEN s.qualname = ? THEN 260
                    WHEN s.name LIKE ? THEN 220
                    WHEN s.qualname LIKE ? THEN 180
                    WHEN s.source LIKE ? THEN 100
                    ELSE 0
                END AS score
            FROM symbols AS s
            JOIN files AS f ON f.id = s.file_id
            WHERE s.name LIKE ? OR s.qualname LIKE ? OR s.source LIKE ?
            ORDER BY score DESC, f.path ASC, s.start_line ASC
            LIMIT ?
            """,
            (q, q, like, like, like, like, like, like, limit),
        )
    )
