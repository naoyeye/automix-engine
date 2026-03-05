# AutoMix Engine

一个自动 DJ 混音引擎，支持音频分析、智能播放列表生成和无缝过渡混音。

## 功能特性

- **音频解码**: 支持 MP3、FLAC、AAC、M4A、OGG、WAV、AIFF、DSD 格式
- **音频分析**: BPM 检测、节拍检测、调性识别、MFCC、Chroma、能量曲线
- **智能匹配**: 基于多维特征的歌曲相似度计算
- **过渡点选择**: 自动识别最佳进/出点
- **实时混音**: 双 Deck 架构，支持 Crossfade 和 EQ 过渡
- **Time-stretch**: 无变调的 BPM 对齐（需要 Rubber Band）
- **Mix 开关**: 可关闭混音模式，作为普通播放器使用（仅元数据扫描、曲间硬切、从 0:00 播满整首）

## 依赖

### 必需

- CMake >= 3.20
- C++17 编译器
- FFmpeg (libavformat, libavcodec, libswresample)
- SQLite3
- Essentia (核心音频分析引擎)

### 可选

- Rubber Band (time-stretch 功能)
- Chromaprint / fpcalc (声纹识别，用于补全缺失的音频元数据)

## 分发说明 (Distribution)

对于最终用户（macOS/iOS App 用户），本项目将采用 **XCFramework** 形式分发，所有依赖（包括 Essentia 和 FFmpeg）都将以静态库形式集成在 App 二进制文件中。用户无需手动安装任何依赖。

## macOS 开发环境配置

```bash
brew install cmake ffmpeg sqlite3

# 可选: time-stretch 功能
brew install rubberband

# 可选: 声纹识别（用于缺失元数据补全）
brew install chromaprint
```

### Essentia（必需，完整音频分析）

[https://github.com/MTG/essentia/](https://github.com/MTG/essentia/)

Essentia 不在 Homebrew 官方仓库，需要从源码编译。相关文档
[https://github.com/MTG/essentia/blob/master/doc/sphinxdoc/installing.rst](https://github.com/MTG/essentia/blob/master/doc/sphinxdoc/installing.rst)

可能遇到 C++ 标准版本不匹配问题导致执行 python3 waf 时报错

`error: Eigen requires at least c++14 support.`

`std::enable_if_t`, `std::integer_sequence` 等 C++14 特性的缺失。

解决方案

需要告诉 waf 使用更高的 C++标准（至少 C++14，建议 C++17）。
请在执行配置时添加 CXXFLAGS 环境变量：



1. 清理旧的构建配置：
`rm -rf cmake-build`

2. 重新配置 (指定 C++17)

`CXXFLAGS="-std=c++17" python3 waf configure --build-static --with-examples --with-vamp`

或使用 Conda：

```bash
conda install -c conda-forge essentia
```

> 注意：项目现在默认要求安装 Essentia 以确保准确的 BPM 和 Key 检测功能。

## 编译

```bash
mkdir cmake-build && cd cmake-build
cmake ..
# 如果遇到架构不兼容问题 (Apple Silicon)，尝试:
# cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 ..

cmake --build .
```

## 使用

### 数据库路径

CLI 工具与 Demo 共用同一套数据库路径规则，便于扫描与播放数据一致：


| 优先级 | 来源                  | 说明               |
| --- | ------------------- | ---------------- |
| 1   | `-d` / `--database` | CLI 命令行参数（仅 CLI） |
| 2   | `AUTOMIX_DB` 环境变量   | 两者均支持            |
| 3   | 平台默认路径              | 见下表              |


**默认路径**：


| 平台    | 默认路径                                                                       |
| ----- | -------------------------------------------------------------------------- |
| macOS | `~/Library/Application Support/Automix/automix.db`                         |
| Linux | `~/.local/share/automix/automix.db`（或 `$XDG_DATA_HOME/automix/automix.db`） |


**共享播放列表**：CLI 生成播放列表时会保存到 `automix_playlist.txt`（与数据库同目录）。Demo 播放时优先读取该文件，实现 CLI 与 Demo 播放列表一致。Demo 也可在界面中创建播放列表（指定 seed 和 count），同样会保存到共享文件。

设置 `AUTOMIX_DB` 可指定自定义数据库路径，例如：

```bash
export AUTOMIX_DB=/path/to/my/automix.db
./automix-scan /path/to/music
```

### CLI 工具

```bash
# 扫描音乐目录（使用默认数据库路径）
./automix-scan /path/to/music

# 仅元数据扫描（不分析 BPM/调性，适用于 Mix 关闭模式）
./automix-scan -m /path/to/music
# 或
./automix-scan --metadata-only /path/to/music

# 指定数据库路径
./automix-scan -d ./automix.db /path/to/music

# 列出曲库
./automix-playlist --list

# 生成播放列表
./automix-playlist --seed {start_track_id} --count {track_count} --random-seed {num}

# 播放（macOS）
./automix-play --seed {start_track_id} --count {track_count} --random-seed {num}

# 播放时设置变速回归时长（秒）
./automix-play --seed {start_track_id} --stretch-recover 6

# 快速检验过渡效果
./scripts/preview_transition.sh song1.mp3 song2.mp3 output.wav
```

### 便捷检验工具

为了方便开发者快速调整和检验混合算法，项目提供了离线渲染工具链。

**1. 离线渲染工具 (`automix-render-transition`)**

该工具可以直接调用引擎内核，以非实时速度（比播放速度快很多）渲染出包含过渡效果的音频片段。

```bash
# 编译
cd cmake-build && make automix-render-transition

# 使用 (通常通过脚本调用，数据库路径需显式指定)
./automix-render-transition <db_path> {track_id_1} {track_id_2} output.wav
# 例如使用默认数据库：
./automix-render-transition ~/Library/Application\ Support/Automix/automix.db 1 2 output.wav
```

**2. 一键预览脚本 (`scripts/preview_transition.sh`)**

无需手动管理数据库，直接输入两个音频文件即可生成过渡预览。用法详见 `scripts/README.md`。

生成的 WAV 文件可以直接拖入 Audacity 等音频编辑软件中，直观地观察 Crossfade 曲线、节奏对齐情况和能量变化。

### C API

```c
#include "automix/automix.h"

// 创建引擎
AutoMixEngine* engine = automix_create("library.db");

// 扫描音乐（第 4 参数 metadata_only：0=全量分析，1=仅元数据）
automix_scan(engine, "/path/to/music", 1, 0);

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

### Swift Automix Demo (macOS)

基于 SwiftUI 的图形化演示应用，展示 AutoMix 引擎的完整功能。

**前置条件**：需先完成 C++ 库的编译（见上文「编译」），并确保 `cmake-build` 目录下已生成 `libautomix.a`。

```bash
# 1. 编译 C++ 库
mkdir -p cmake-build && cd cmake-build

cmake ..
## 或者指定架构：
cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 ..

cmake --build .

# 2. 返回项目根目录，使用 Swift Package Manager 运行 Demo
cd ..
swift test --arch x86_64 && swift build --target AutomixDemo && swift run AutomixDemo
```



**功能说明**：


| 功能      | 说明                                            |
| ------- | --------------------------------------------- |
| 扫描曲库    | 点击「Scan Music Directory」选择音乐目录，自动分析 BPM、调性等特征 |
| Mix 过渡效果 | 设置面板中的开关。关闭后：扫描仅做元数据（path/时长），不分析 BPM/调性；曲间硬切；每首从 0:00 播至结束，时长为实际时长 |
| 播放/暂停   | 首次播放时自动以曲库第一首为种子生成 10 首播放列表                   |
| 上一首/下一首 | 在播放列表中切换曲目，Mix 开启时无缝过渡，关闭时硬切                    |
| 状态显示    | 显示当前曲目名称/艺术家/封面、播放进度及在播放列表中的位置等               |


**注意**：Demo 与 CLI 共用同一默认数据库路径（见上文「数据库路径」）。未设置 `AUTOMIX_DB` 时，数据保存在 `~/Library/Application Support/Automix/automix.db`。

#### 曲目元数据获取流程

Demo 播放时会自动为每首曲目获取 title、artist、album 和专辑封面。获取流程按优先级依次尝试，结果缓存到 SQLite，后续播放直接读取缓存：

```
1. 文件内嵌标签（AVFoundation）
   └─ title + artist + artwork 齐全 → 直接使用
   └─ title + artist 有但缺封面 → 跳到步骤 5 补搜封面

2. 数据库缓存
   └─ 有完整内容或已完整尝试过所有来源 → 直接使用

3. AcoustID 声纹识别（需要 fpcalc + API key）
   └─ 通过音频指纹查询 AcoustID → 获取 title/artist/album
   └─ 通过 MusicBrainz release-group MBID → Cover Art Archive 获取封面
   └─ 封面仍缺失 → 跳到步骤 5

4. 文件名解析
   └─ 支持的模式：
      "Artist [Year] Album [Track#] Title.ext"
      "Artist - Album - Track# Title.ext"
      "Artist - Title.ext"
      "Track# Title.ext"
   └─ 提取 artist/album/title 后跳到步骤 5

5. 封面搜索（MusicBrainz → iTunes）
   └─ MusicBrainz Search API → 查找 release-group MBID → Cover Art Archive
   └─ 若失败 → iTunes Search API（免费，无需 key）→ 获取专辑封面
```

**配置要求**：


| 依赖               | 用途                    | 安装方式                                          |
| ---------------- | --------------------- | --------------------------------------------- |
| fpcalc           | 音频指纹提取（AcoustID 步骤 3） | `brew install chromaprint`                    |
| AcoustID API key | 声纹查询（步骤 3）            | 在 [acoustid.org](https://acoustid.org/) 注册后获取 |


API key 配置：创建 `keys.json` 文件，放在以下任一位置：

1. `~/Library/Application Support/AutomixDemo/keys.json`（推荐）
2. 当前工作目录下
3. App Bundle 内

```json
{"apikey": "你的AcoustID客户端API密钥"}
```

步骤 4（文件名解析）和步骤 5（MusicBrainz/iTunes 封面搜索）不需要任何额外配置。

### Swift 集成 (macOS/iOS)

项目提供 Swift 封装 `AutoMixEngine`（`import Automix`），推荐使用：

```swift
import Automix

// 创建引擎（自动管理 C 引擎生命周期）
let engine = try AutoMixEngine(dbPath: "library.db")

// 扫描曲库（metadataOnly: true 时仅做元数据扫描，不分析 BPM/调性）
try engine.scan(musicDir: "/path/to/music", recursive: true, metadataOnly: false)

// 生成播放列表并播放
let playlist = try engine.generatePlaylist(seedTrackId: seedId, count: 10)
try engine.play(playlist: playlist)

// 订阅状态更新（Combine）
engine.statusPublisher
    .sink { status in
        print("Track \(status.currentTrackId), position: \(status.position)s")
    }
    .store(in: &cancellables)
```

也可直接使用底层 C API（`import CAutomix`），参见 `include/automix/automix.h`。

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