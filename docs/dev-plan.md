# AutoMix Engine 开发计划

## 第一阶段：基础架构 ✅ (已完成)

[x] 项目结构搭建
[x] CMake 构建系统
[x] 公开 API 头文件定义 (automix.h, types.h)
[x] 基础模块文件创建

## 第二阶段：核心模块实现 ✅ (已完成)

### Core 模块
[x] SQLite 数据库存储实现 (store.cpp)
[x] 工具函数完善 (utils.cpp)

### Decoder 模块
[x] FFmpeg 集成
[x] 多格式解码支持 (MP3, FLAC, AAC, M4A, OGG, WAV, AIFF, DSD)

### Analyzer 模块
[x] BPM 检测算法 (自相关算法)
[x] 能量曲线分析 (RMS)
[x] 调性识别 (Krumhansl-Kessler + Camelot)
[x] (可选) Essentia 集成

## 第三阶段：智能匹配 ✅ (已完成)

### Matcher 模块
[x] 多维特征相似度计算 (BPM/Key/MFCC/Chroma/Energy/Duration 六维加权)
[x] 智能播放列表生成算法 (能量弧线/BPM渐进/综合评分/可复现随机)
[x] 最佳过渡点识别 (乐句边界对齐/多因素评分/Pitch Shift建议/EQ过渡)

## 第四阶段：实时混音 ✅ (已完成)

### Mixer 模块
[x] Deck 播放器实现 (音量平滑 + 3-band EQ + Rubber Band 预分配优化)
[x] Crossfader 控制 (EqualPower/Linear/EQSwap/HardCut + MixParams + 自动化)
[x] 调度器 (Scheduler) (音频线程安全 + render/poll 分离 + 双 Deck 过渡)
[x] Time-stretch (Rubber Band 集成)
[x] 音频输出 (CoreAudio AudioUnit)
[x] Engine 集成 (AudioOutput + poll + C API 扩展)

## 第五阶段：CLI 工具 & 测试
[ ] automix-scan 扫描工具
[ ] automix-playlist 播放列表工具
[ ] automix-play 播放工具

## 第六阶段：优化与扩展
[ ] 性能优化
[ ] Swift 包装层 (macOS/iOS 集成)
[ ] 文档完善