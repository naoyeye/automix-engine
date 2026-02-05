AutoMix Engine 开发计划

第一阶段：基础架构 ✅ (已完成)

[x] 项目结构搭建
[x] CMake 构建系统
[x] 公开 API 头文件定义 (automix.h, types.h)
[x] 基础模块文件创建

第二阶段：核心模块实现 ✅ (已完成)

Core 模块
[x] SQLite 数据库存储实现 (store.cpp)
[x] 工具函数完善 (utils.cpp)

Decoder 模块
[x] FFmpeg 集成
[x] 多格式解码支持 (MP3, FLAC, AAC, M4A, OGG, WAV, AIFF, DSD)

Analyzer 模块
[x] BPM 检测算法 (自相关算法)
[x] 能量曲线分析 (RMS)
[x] 调性识别 (Krumhansl-Kessler + Camelot)
[ ] (可选) Essentia 集成

第三阶段：智能匹配
Matcher 模块
[ ] 多维特征相似度计算
[ ] 智能播放列表生成算法
[ ] 最佳过渡点识别

第四阶段：实时混音
Mixer 模块
[ ] Deck 播放器实现
[ ] Crossfader 控制
[ ] 调度器 (Scheduler)
[ ] Time-stretch (Rubber Band 集成)

第五阶段：CLI 工具 & 测试
[ ] automix-scan 扫描工具
[ ] automix-playlist 播放列表工具
[ ] automix-play 播放工具
[x] 单元测试完善 (Phase 2 模块测试已添加)

第六阶段：优化与扩展
[ ] 性能优化
[ ] Swift 包装层 (macOS/iOS 集成)
[ ] 文档完善