# AutomixMac App Target

这个目录包含可直接运行的 macOS `.app` 工程（`AutomixMac.xcodeproj`）。

## 生成工程

如果修改了 `project.yml`，在此目录执行：

```bash
xcodegen generate
```

## 运行

1. 用 Xcode 打开 `AutomixMac.xcodeproj`
2. 选择 `AutomixMac` scheme
3. Run

该方式会以 App Bundle 启动（带 `CFBundleIdentifier`），系统通知可用。

## App Icon

当前已创建 `Assets.xcassets/AppIcon.appiconset` 模板。请按 Xcode 规范填充图标文件（16/32/128/256/512 的 1x/2x）。

## 命名建议

- 产品名建议：`AutoMix`（面向用户）
- 工程/目录名建议：`AutomixMac`（面向开发）
- 现有 `Examples/AutomixDemo` 可保留为实验入口，后续稳定后再迁移/重命名。
