# AutoMix 脚本工具

本目录包含 AutoMix Engine 的辅助脚本。

## preview_transition.sh

在两首音频文件之间渲染过渡片段，无需使用主曲库。脚本会创建临时数据库、扫描两个文件、渲染过渡并输出 WAV，最后自动清理临时文件。

**前置条件**：需先完成项目构建（`cmake-build` 中存在 `automix-scan`、`automix-render-transition` 等工具）。

### 使用方式

```bash
# 基本用法（输出默认保存为 transition_preview.wav）
./scripts/preview_transition.sh <音频文件1> <音频文件2>

# 指定输出文件名
./scripts/preview_transition.sh song1.flac song2.mp3 output.wav
```

### 参数说明

| 参数 | 说明 |
|------|------|
| `file1` | 第一首音频文件路径 |
| `file2` | 第二首音频文件路径 |
| `output.wav` | 可选，输出 WAV 文件路径（默认：当前目录下的 `transition_preview.wav`） |

### 示例

```bash
# 在项目根目录下运行
cd /path/to/automix-engine
./scripts/preview_transition.sh audio-files/track1.flac audio-files/track2.mp3
# 生成 transition_preview.wav

./scripts/preview_transition.sh track1.aiff track2.flac my_transition.wav
# 生成 my_transition.wav
```

---

## dedupe_tracks.py

去除数据库中指向同一物理文件但路径表示不同的重复曲目记录。例如，同一文件可能因扫描时使用 `./audio-files` 与 `/full/path/audio-files` 而产生两条记录，本脚本可识别并删除重复项。

### 使用方式

```bash
# 预览将要删除的重复记录（不实际修改数据库）
python scripts/dedupe_tracks.py -n
python scripts/dedupe_tracks.py --dry-run

# 执行去重
python scripts/dedupe_tracks.py

# 指定数据库路径
python scripts/dedupe_tracks.py -d /path/to/automix.db

# 指定项目根目录（用于解析相对路径）
python scripts/dedupe_tracks.py -r /path/to/project

# 组合使用
python scripts/dedupe_tracks.py -d ./automix.db -r /Users/me/Project/automix-engine -n
```

### 参数说明

| 参数 | 简写 | 说明 |
|------|------|------|
| `--db` | `-d` | 数据库文件路径 |
| `--project-root` | `-r` | 解析相对路径时使用的项目根目录（默认：当前工作目录） |
| `--dry-run` | `-n` | 仅预览，不执行删除 |

### 默认值

- **数据库路径**：`AUTOMIX_DB` 环境变量 > 平台默认
  - macOS: `~/Library/Application Support/Automix/automix.db`
  - Linux: `$XDG_DATA_HOME/automix/automix.db` 或 `~/.local/share/automix/automix.db`
- **项目根目录**：当前工作目录

### 示例

```bash
# 在项目根目录下运行，使用默认数据库
cd /Users/hanjiyun/Project/automix-engine
python scripts/dedupe_tracks.py -n   # 先预览
python scripts/dedupe_tracks.py      # 确认后执行

# 使用自定义数据库
export AUTOMIX_DB=/path/to/my/automix.db
python scripts/dedupe_tracks.py
```
