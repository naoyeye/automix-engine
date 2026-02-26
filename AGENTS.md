# AGENTS.md

## Cursor Cloud specific instructions

### Project overview

AutoMix Engine is a C++17 static library (`libautomix`) for automatic DJ mixing, with CLI tools. See `README.md` for full details.

### Build

```bash
cd /workspace
mkdir -p cmake-build && cd cmake-build
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..
cmake --build . -j$(nproc)
```

**Important**: On this Linux VM, the default compiler (Clang 18) cannot link because it looks for gcc-14's `libstdc++` while only gcc-13 is installed. Always pass `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` to CMake.

### Tests

```bash
cd /workspace/cmake-build && ctest --output-on-failure
```

- `test_basic`, `test_phase2`, `test_phase3` pass.
- `test_phase4` has a pre-existing assertion failure in `test_scheduler_previous()` unrelated to environment setup.

### CLI tools (in `cmake-build/`)

| Tool | Purpose |
|---|---|
| `automix-scan <dir>` | Scan audio files and analyze BPM/key/features into SQLite DB |
| `automix-playlist --list` | List tracks in the library |
| `automix-playlist --seed <id> --count <n>` | Generate a playlist |
| `automix-play --seed <id>` | Real-time playback (macOS only, requires CoreAudio) |
| `automix-render-transition <db> <id1> <id2> <out.wav>` | Offline render a transition |

**Database path**: CLI and Demo share the same default: `AUTOMIX_DB` env > platform default (macOS: `~/Library/Application Support/Automix/automix.db`, Linux: `~/.local/share/automix/automix.db`). Use `-d` to override for CLI.

### Dependencies (pre-installed)

All required system dependencies are pre-installed on the VM:
- CMake 3.28, GCC 13 (g++), pkg-config
- FFmpeg dev libs (libavformat, libavcodec, libavutil, libswresample)
- SQLite3 dev
- Essentia 2.1-beta6-dev (built from source, installed to `/usr/local`)

Rubber Band (optional, for time-stretch) is **not** installed.

### Gotchas

- Audio playback (`automix-play`) requires macOS CoreAudio and will not produce audio output on Linux. All other tools work on Linux.
- The project has no linter configuration (no clang-tidy, clang-format, or similar). There is no lint step.
- Essentia is installed in `/usr/local` and found via `pkg-config`. If `pkg-config --libs essentia` fails, run `sudo ldconfig`.
