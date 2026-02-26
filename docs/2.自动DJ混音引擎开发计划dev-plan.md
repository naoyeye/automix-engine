# AutoMix Engine 开发计划

## 第一阶段：基础架构 ✅ (已完成)

- [x] 项目结构搭建
- [x] CMake 构建系统
- [x] 公开 API 头文件定义 (automix.h, types.h)
- [x] 基础模块文件创建

## 第二阶段：核心模块实现 ✅ (已完成)

### Core 模块
- [x] SQLite 数据库存储实现 (store.cpp)
- [x] 工具函数完善 (utils.cpp)

### Decoder 模块
- [x] FFmpeg 集成
- [x] 多格式解码支持 (MP3, FLAC, AAC, M4A, OGG, WAV, AIFF, DSD)

### Analyzer 模块
- [x] BPM 检测算法 (自相关算法)
- [x] 能量曲线分析 (RMS)
- [x] 调性识别 (Krumhansl-Kessler + Camelot)
- [x] (可选) Essentia 集成

## 第三阶段：智能匹配 ✅ (已完成)

### Matcher 模块
- [x] 多维特征相似度计算 (BPM/Key/MFCC/Chroma/Energy/Duration 六维加权)
- [x] 智能播放列表生成算法 (能量弧线/BPM渐进/综合评分/可复现随机)
- [x] 最佳过渡点识别 (乐句边界对齐/多因素评分/Pitch Shift建议/EQ过渡)

## 第四阶段：实时混音 ✅ (已完成)

### Mixer 模块
- [x] Deck 播放器实现 (音量平滑 + 3-band EQ + Rubber Band 预分配优化)
- [x] Crossfader 控制 (EqualPower/Linear/EQSwap/HardCut + MixParams + 自动化)
- [x] 调度器 (Scheduler) (音频线程安全 + render/poll 分离 + 双 Deck 过渡)
- [x] Time-stretch (Rubber Band 集成)
- [x] 音频输出 (CoreAudio AudioUnit)
- [x] Engine 集成 (AudioOutput + poll + C API 扩展)

## 第五阶段：CLI 工具 & 测试 ✅ (已完成)

- [x] automix-scan 扫描工具 (参数解析 + 进度回调 + 递归扫描)
- [x] automix-playlist 播放列表工具 (曲库列表 + 播放列表生成 + 规则配置)
- [x] automix-play 播放工具 (CoreAudio 播放 + 自动过渡 + EQ Swap + 信号处理)
- [x] 测试套件 (test_basic + test_phase2 + test_phase3 + test_phase4, 全部通过)

# 下一步开发计划

根据原始计划和当前状态，以下是建议的后续开发路线：

## 第六阶段：优化与扩展

### 1. 项目管理基础设施
- [x] 初始化 Git 仓库，创建首次提交
- [x] 更新 dev-plan.md，标记第五阶段为已完成

### 2. 性能优化
- [ ] 音频解码性能 profiling（大曲库 100+ 首歌扫描测试）
- [ ] Scheduler 的 lock-free queue 优化验证
- [ ] 内存使用分析（大文件 FLAC/DSD 场景）
- [ ] 音频线程延迟测量（目标 < 20ms）

### 3. Swift 包装层（macOS/iOS 集成）
- [ ] 设计 Swift wrapper API（AutoMixEngine class）
- [ ] 创建 Swift Package（SPM 支持）
- [ ] 类型桥接（C struct → Swift struct）
- [ ] 回调机制的 Swift 封装（闭包/Combine/async-await）
- [ ] macOS 示例 App（SwiftUI + CoreAudio 验证）

### 4. 文档完善
- [ ] API 参考文档（每个 C API 函数的详细说明）
- [ ] 集成指南（如何在 App 中使用 libautomix）
- [ ] 架构设计文档（模块关系、线程模型、数据流）

## 中期（扩展功能）

### 5. 更多过渡策略
- [ ] Filter Sweep 过渡（低通/高通滤波器扫频）
- [ ] Echo Out 过渡（回声渐隐效果）
- [ ] 用户可自定义过渡策略的插件机制

### 6. 跨平台支持
- [ ] Linux 音频后端（PulseAudio / ALSA）
- [ ] Windows 音频后端（WASAPI）
- [ ] Android 音频后端（AAudio / Oboe）