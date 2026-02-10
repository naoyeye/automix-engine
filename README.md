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
- Essentia (核心音频分析引擎)

### 可选
- Rubber Band (time-stretch 功能)

## 分发说明 (Distribution)

对于最终用户（macOS/iOS App 用户），本项目将采用 **XCFramework** 形式分发，所有依赖（包括 Essentia 和 FFmpeg）都将以静态库形式集成在 App 二进制文件中。用户无需手动安装任何依赖。

## macOS 开发环境配置

```bash
brew install cmake ffmpeg sqlite3

# 可选: time-stretch 功能
brew install rubberband
```

### Essentia（必需，完整音频分析）

https://github.com/MTG/essentia/

Essentia 不在 Homebrew 官方仓库，需要从源码编译。相关文档
https://github.com/MTG/essentia/blob/master/doc/sphinxdoc/installing.rst

可能遇到 C++ 标准版本不匹配问题导致执行 python3 waf 时报错

`error: Eigen requires at least c++14 support.`

`std::enable_if_t`, `std::integer_sequence` 等 C++14 特性的缺失。


解决方案

需要告诉 waf 使用更高的 C++ 标准（至少 C++14，建议 C++17）。
请在执行配置时添加 CXXFLAGS 环境变量：

1. 
清理旧的构建配置：
`rm -rf build`

2. 重新配置 (指定 C++17)

`CXXFLAGS="-std=c++17" python3 waf configure --build-static --with-examples --with-vamp`


或使用 Conda：

```bash
conda install -c conda-forge essentia
```

> 注意：项目现在默认要求安装 Essentia 以确保准确的 BPM 和 Key 检测功能。

## 编译

```bash
mkdir build && cd build
cmake ..
# 如果遇到架构不兼容问题 (Apple Silicon)，尝试:
# cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 ..

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
./automix-playlist --seed {start_track_id} --count {track_count} --random-seed {num}

# 播放（macOS）
./automix-play --seed {start_track_id} --count {track_count} --random-seed {num}

# 播放时设置变速回归时长（秒）
./automix-play --seed {start_track_id} --stretch-recover 6

# 快速检验过渡效果
./preview_transition.sh song1.mp3 song2.mp3 output.wav
```

### 便捷检验工具

为了方便开发者快速调整和检验混合算法，项目提供了离线渲染工具链。

**1. 离线渲染工具 (`automix-render-transition`)**

该工具可以直接调用引擎内核，以非实时速度（比播放速度快很多）渲染出包含过渡效果的音频片段。

```bash
# 编译
cd build && make automix-render-transition

# 使用 (通常通过脚本调用)
./automix-render-transition automix.db {track_id_1} {track_id_2} output.wav
```

**2. 一键预览脚本 (`preview_transition.sh`)**

无需手动管理数据库，直接输入两个音频文件即可生成过渡预览。

```bash
# 用法: ./preview_transition.sh <file1> <file2> [output.wav]
./preview_transition.sh song1.mp3 song2.mp3 preview.wav
```

生成的 WAV 文件可以直接拖入 Audacity 等音频编辑软件中，直观地观察 Crossfade 曲线、节奏对齐情况和能量变化。

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
