#!/usr/bin/env python3
"""
Deduplicate tracks in automix database.

Removes duplicate entries that point to the same physical file but were stored
with different path representations (e.g. ./audio-files/x vs /full/path/audio-files/x).

Usage:
  python scripts/dedupe_tracks.py [--db PATH] [--project-root DIR]
  python scripts/dedupe_tracks.py -d /path/to/automix.db

Defaults:
  - db: AUTOMIX_DB env, or ~/Library/Application Support/Automix/automix.db (macOS)
        or ~/.local/share/automix/automix.db (Linux)
  - project-root: current working directory (for resolving relative paths)
"""

import argparse
import os
import sqlite3
import sys
from pathlib import Path


def get_default_db_path() -> str:
    home = Path.home()
    if sys.platform == "darwin":
        return str(home / "Library/Application Support/Automix/automix.db")
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return str(Path(xdg) / "automix" / "automix.db")
    return str(home / ".local/share/automix/automix.db")


def normalize_path(path_str: str, project_root: Path) -> str:
    """Resolve path to canonical absolute form for comparison."""
    p = Path(path_str)
    if not p.is_absolute():
        p = (project_root / p).resolve()
    try:
        return str(p.resolve())  # resolve symlinks
    except OSError:
        return str(p)


def main() -> int:
    parser = argparse.ArgumentParser(description="Deduplicate tracks in automix database")
    parser.add_argument(
        "-d", "--db",
        default=os.environ.get("AUTOMIX_DB") or get_default_db_path(),
        help="Database file path",
    )
    parser.add_argument(
        "-r", "--project-root",
        default=Path.cwd(),
        type=Path,
        help="Project root for resolving relative paths (default: cwd)",
    )
    parser.add_argument(
        "-n", "--dry-run",
        action="store_true",
        help="Show what would be deleted without making changes",
    )
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Error: Database not found: {db_path}", file=sys.stderr)
        return 1

    project_root = args.project_root.resolve()
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row

    rows = conn.execute("SELECT id, path FROM tracks").fetchall()
    canonical_to_entries: dict[str, list[tuple[int, str]]] = {}

    for row in rows:
        path_str = row["path"]
        try:
            canonical = normalize_path(path_str, project_root)
        except Exception:
            canonical = path_str
        if canonical not in canonical_to_entries:
            canonical_to_entries[canonical] = []
        canonical_to_entries[canonical].append((row["id"], path_str))

    to_delete: list[int] = []
    for canonical, entries in canonical_to_entries.items():
        if len(entries) > 1:
            # Prefer absolute path (keep first), then smallest id
            def sort_key(x):
                is_abs = x[1].startswith("/") or (len(x[1]) > 1 and x[1][1] == ":")
                return (not is_abs, x[0])
            entries.sort(key=sort_key)
            keep_id = entries[0][0]
            for tid, tpath in entries[1:]:
                to_delete.append(tid)
                if args.dry_run:
                    print(f"  Would delete [{tid}] {tpath} (duplicate of [{keep_id}])")

    if not to_delete:
        print("No duplicate tracks found.")
        return 0

    print(f"Found {len(to_delete)} duplicate track(s) to remove.")

    if args.dry_run:
        print("Dry run - no changes made.")
        return 0

    # track_metadata has ON DELETE CASCADE, so deleting tracks will remove metadata
    cursor = conn.cursor()
    for tid in to_delete:
        cursor.execute("DELETE FROM tracks WHERE id = ?", (tid,))
    conn.commit()
    conn.close()

    print(f"Deleted {len(to_delete)} duplicate track(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
