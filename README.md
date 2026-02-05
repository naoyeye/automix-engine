# AutoMix Engine

一个自动 DJ 混音引擎，支持音频分析、智能播放列表生成和无缝过渡混音。

## 功能特性

- **音频解码**: 支持 MP3、FLAC、AAC、M4A、OGG、WAV、AIFF、DSD 格式
- **音频分析**: BPM 检测、节拍检测、调性识别、MFCC、Chroma、能量曲线
- **智能匹配**: 基于多维特征的歌曲相似度计算
- **过渡点选择**: 自动识别最佳进/出点
- **实时混音**: 双 Deck 架构，支持 Crossfade 和 EQ 过渡
- **Time-stretch**: 无变调的 BPM 对齐（需要 Rubber Band）

## 依赖

### 必需
- CMake >= 3.20
- C++17 编译器
- FFmpeg (libavformat, libavcodec, libswresample)
- SQLite3

### 可选
- Essentia (完整音频分析)
- Rubber Band (time-stretch 功能)

## macOS 安装依赖

```bash
brew install cmake ffmpeg sqlite3

# 可选: time-stretch 功能
brew install rubberband
```

### Essentia（可选，完整音频分析）

Essentia 不在 Homebrew 官方仓库，需要从源码编译：

```bash
# 安装依赖
brew install eigen fftw libyaml libsamplerate taglib chromaprint

# 编译安装
git clone https://github.com/MTG/essentia.git
cd essentia
python3 waf configure --build-static
python3 waf
sudo python3 waf install
```

或使用 Conda：

```bash
conda install -c conda-forge essentia
```

> 注意：不安装 Essentia 也可正常使用，项目会使用内置的简化分析功能。

## 编译

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## 使用

### CLI 工具

```bash
# 扫描音乐目录
./automix-scan /path/to/music

# 列出曲库
./automix-playlist --list

# 生成播放列表
./automix-playlist --seed 1 --count 10

# 播放（macOS）
./automix-play --seed 1 --count 10
```

### C API

```c
#include "automix/automix.h"

// 创建引擎
AutoMixEngine* engine = automix_create("library.db");

// 扫描音乐
automix_scan(engine, "/path/to/music", 1);

// 生成播放列表
PlaylistHandle playlist = automix_generate_playlist(engine, seed_id, 10, NULL);

// 播放
automix_play(engine, playlist);

// 渲染音频 (在音频回调中)
automix_render(engine, buffer, frames);

// 清理
automix_playlist_free(playlist);
automix_destroy(engine);
```

### Swift 集成 (macOS/iOS)

```swift
// 使用 C API 或创建 Swift wrapper
let engine = automix_create("library.db")
defer { automix_destroy(engine) }

automix_scan(engine, musicPath, 1)
// ...
```

## 项目结构

```
automix-engine/
├── include/automix/     # 公开头文件
│   ├── automix.h        # C API
│   └── types.h          # 内部类型
├── src/
│   ├── core/            # 核心工具和存储
│   ├── decoder/         # 音频解码
│   ├── analyzer/        # 特征分析
│   ├── matcher/         # 相似度和播放列表
│   ├── mixer/           # 实时混音引擎
│   ├── api/             # C API 实现
│   └── cli/             # CLI 工具
├── tests/               # 测试
└── examples/            # 示例代码
```

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    播放器 App (Swift)                    │
├─────────────────────────────────────────────────────────┤
│                    AutoMix Engine (C++)                  │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌───────────────┐ │
│  │ Decoder │ │Analyzer │ │ Matcher │ │    Mixer      │ │
│  │ (FFmpeg)│ │(Essentia)│ │         │ │ Deck A / B   │ │
│  └────┬────┘ └────┬────┘ └────┬────┘ │ Crossfader   │ │
│       │           │           │       │ Scheduler    │ │
│       └───────────┴───────────┘       └───────────────┘ │
│                    │                          │         │
│              ┌─────┴─────┐            ┌───────┴───────┐ │
│              │  SQLite   │            │ Audio Output  │ │
│              │ (特征库)  │            │ (CoreAudio)   │ │
│              └───────────┘            └───────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## License

MIT
