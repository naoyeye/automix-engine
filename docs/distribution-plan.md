# AutoMix Engine 分发与集成方案 (macOS / iOS)

本文档记录了如何将 AutoMix Engine 及其重型依赖（Essentia, FFmpeg）打包分发给最终用户，确保用户无需安装开发环境即可使用。

## 1. 核心目标
- **零依赖安装**：用户下载 App 后直接运行。
- **高性能**：利用静态链接减少动态加载开销。
- **多平台支持**：支持 Mac (Intel/Apple Silicon) 和 iPhone/iPad。

## 2. 方案：XCFramework 打包

### 2.1 交叉编译依赖
需要为每个目标架构编译静态库 (`.a`)：
- **macOS**: `x86_64`, `arm64`
- **iOS**: `arm64`
- **iOS Simulator**: `x86_64`, `arm64`

### 2.2 打包流程
1. **编译 Essentia**: 使用 Essentia 提供的交叉编译工具链（`waf configure --cross-compile`）。
2. **编译 FFmpeg**: 使用针对 iOS/macOS 优化的编译脚本（如 `FFmpeg-iOS-build-script`）。
3. **合并库**: 使用 `lipo` 合并相同平台的架构。
4. **创建 XCFramework**:
   ```bash
   xcodebuild -create-xcframework \
     -library libautomix_macos.a -headers include/ \
     -library libautomix_ios.a -headers include/ \
     -output AutoMixEngine.xcframework
   ```

## 3. 集成方式

### 3.1 Swift Package Manager (推荐)
创建一个 `Package.swift`，将生成的 `.xcframework` 作为 `binaryTarget` 引入：
```swift
.binaryTarget(
    name: "AutoMixEngine",
    path: "Artifacts/AutoMixEngine.xcframework"
)
```

### 3.2 手动集成
直接将 `.xcframework` 拖入 Xcode 工程的 "Frameworks, Libraries, and Embedded Content" 区域。

## 4. 法律与许可 (License)
- **FFmpeg**: 必须使用 `--enable-gpl` 或 `--enable-nonfree` 之外的配置，以符合 App Store 政策（通常使用 LGPL）。
- **Essentia**: 确认使用 MPL2 许可版本。

## 5. 开发建议
- 在开发 App UI 时，保持 C++ 引擎作为子工程。
- 只有在发布 Beta 测试或正式版时，才重新构建全平台的 XCFramework。
