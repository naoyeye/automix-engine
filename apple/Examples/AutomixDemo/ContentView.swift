import SwiftUI
import Automix
import AppKit

let kMinWindowWidth: CGFloat = 300
let kMinWindowHeight: CGFloat = 500

/// 磨砂背景材质（由透到厚）：.ultraThinMaterial / .thinMaterial / .regularMaterial / .thickMaterial / .ultraThickMaterial
private let kBackgroundMaterial: Material = .thinMaterial

/// 配置窗口为透明，以便磨砂背景能透出桌面
private struct WindowTransparencyModifier: ViewModifier {
    func body(content: Content) -> some View {
        content.background(WindowConfigurator())
    }
}

/// 主窗口整体透明度，0~1，越小越透明
private let kWindowAlphaValue: CGFloat = 0.98

private class WindowConfiguratorView: NSView {
    private weak var configuredWindow: NSWindow?
    private var originalIsOpaque: Bool?
    private var originalBackgroundColor: NSColor?
    private var originalAlphaValue: CGFloat?
    private var originalHidesOnDeactivate: Bool?
    private var originalCollectionBehavior: NSWindow.CollectionBehavior?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        // Restore settings on the previously configured window if it changed
        if let previousWindow = configuredWindow, previousWindow !== window {
            if let originalIsOpaque { previousWindow.isOpaque = originalIsOpaque }
            if let originalBackgroundColor { previousWindow.backgroundColor = originalBackgroundColor }
            if let originalAlphaValue { previousWindow.alphaValue = originalAlphaValue }
            if let originalHidesOnDeactivate { previousWindow.hidesOnDeactivate = originalHidesOnDeactivate }
            if let originalCollectionBehavior { previousWindow.collectionBehavior = originalCollectionBehavior }
            configuredWindow = nil
            originalIsOpaque = nil
            originalBackgroundColor = nil
            originalAlphaValue = nil
            originalHidesOnDeactivate = nil
            originalCollectionBehavior = nil
        }

        guard let window = window, configuredWindow == nil else { return }
        originalIsOpaque = window.isOpaque
        originalBackgroundColor = window.backgroundColor
        originalAlphaValue = window.alphaValue
        originalHidesOnDeactivate = window.hidesOnDeactivate
        originalCollectionBehavior = window.collectionBehavior
        window.isOpaque = false
        window.backgroundColor = .clear
        window.alphaValue = kWindowAlphaValue
        window.hidesOnDeactivate = false
        window.collectionBehavior.formUnion([.managed, .fullScreenPrimary, .participatesInCycle])
        configuredWindow = window
    }

    deinit {
        if let window = configuredWindow {
            if let originalIsOpaque { window.isOpaque = originalIsOpaque }
            if let originalBackgroundColor { window.backgroundColor = originalBackgroundColor }
            if let originalAlphaValue { window.alphaValue = originalAlphaValue }
            if let originalHidesOnDeactivate { window.hidesOnDeactivate = originalHidesOnDeactivate }
            if let originalCollectionBehavior { window.collectionBehavior = originalCollectionBehavior }
        }
    }
}

private struct WindowConfigurator: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        WindowConfiguratorView()
    }
    
    func updateNSView(_ nsView: NSView, context: Context) {}
}

/// 手型指针：用 onContinuousHover 在鼠标移动时持续设置光标，
/// 避免被 SwiftUI 内部的 NSHostingView 重置。
/// 参考: https://gist.github.com/Amzd/cb8ba40625aeb6a015101d357acaad88
private extension View {
    func pointingHandCursor() -> some View {
        self.contentShape(Rectangle())
            .onContinuousHover { phase in
                switch phase {
                case .active:
                    NSCursor.pointingHand.set()
                case .ended:
                    NSCursor.arrow.set()
                }
            }
    }
}

/// 角落箭头按钮样式：hover/点击时显示背景色
private struct CornerArrowButtonStyle: ButtonStyle {
    @State private var isHovered = false
    
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(6)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color.white.opacity(backgroundOpacity(isHovered: isHovered, isPressed: configuration.isPressed)))
            )
            .opacity(configuration.isPressed ? 0.8 : 1.0)
            .scaleEffect(configuration.isPressed ? 0.95 : 1.0)
            .onHover { isHovered = $0 }
            .animation(.easeInOut(duration: 0.15), value: isHovered)
            .animation(.easeInOut(duration: 0.1), value: configuration.isPressed)
    }
    
    private func backgroundOpacity(isHovered: Bool, isPressed: Bool) -> Double {
        if isPressed { return 0.25 }
        if isHovered { return 0.15 }
        return 0
    }
}

/// 交互按钮样式：hover 时降低透明度，按下时进一步降低并略微缩小
private struct InteractiveButtonStyle: ButtonStyle {
    @State private var isHovered = false
    
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .opacity(configuration.isPressed ? 0.5 : (isHovered ? 0.7 : 1.0))
            .scaleEffect(configuration.isPressed ? 0.95 : 1.0)
            .onHover { isHovered = $0 }
            .animation(.easeInOut(duration: 0.15), value: isHovered)
            .animation(.easeInOut(duration: 0.1), value: configuration.isPressed)
    }
}

/// 可拖拽的播放进度条（全宽，5pt 高度，带圆形拖拽按钮）
private struct PlaybackProgressBar: View {
    let position: Float
    let duration: Float
    let isDisabled: Bool
    let onSeek: (Float) -> Void
    
    @State private var isDragging = false
    @State private var isHovering = false
    @State private var dragFraction: CGFloat = 0
    
    private var fraction: CGFloat {
        guard duration > 0 else { return 0 }
        if isDragging { return clamped(dragFraction) }
        return clamped(CGFloat(position / duration))
    }
    
    private func clamped(_ v: CGFloat) -> CGFloat { min(max(v, 0), 1) }
    
    private var thumbDiameter: CGFloat {
        if isDragging { return 16 }
        if isHovering { return 14 }
        return 12
    }
    
    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let filledW = fraction * w
            let midY = geo.size.height / 2
            let thumbX = max(thumbDiameter / 2, min(filledW, w - thumbDiameter / 2))
            
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(Color.white.opacity(0.15))
                    .frame(height: 5)
                    .frame(maxWidth: .infinity)
                    .position(x: w / 2, y: midY)
                
                Capsule()
                    .fill(Color.white.opacity(isDisabled ? 0.2 : 0.5))
                    .frame(width: max(0, filledW), height: 5)
                    .position(x: filledW / 2, y: midY)
                
                if !isDisabled && duration > 0 {
                    Circle()
                        .fill(Color.white)
                        .frame(width: thumbDiameter, height: thumbDiameter)
                        .shadow(color: .black.opacity(isDragging ? 0.4 : 0.2), radius: isDragging ? 3 : 2, y: 1)
                        .scaleEffect(isDragging ? 1.05 : 1.0)
                        .position(x: thumbX, y: midY)
                        .animation(.easeInOut(duration: 0.12), value: thumbDiameter)
                        .animation(.easeInOut(duration: 0.1), value: isDragging)
                }
            }
            .contentShape(Rectangle())
            .onContinuousHover { phase in
                guard !isDisabled else { return }
                switch phase {
                case .active:
                    isHovering = true
                    NSCursor.pointingHand.set()
                case .ended:
                    isHovering = false
                    if !isDragging { NSCursor.arrow.set() }
                }
            }
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        guard !isDisabled, duration > 0 else { return }
                        guard w > 0 else {
                            isDragging = true
                            dragFraction = 0
                            return
                        }
                        isDragging = true
                        dragFraction = clamped(value.location.x / w)
                    }
                    .onEnded { value in
                        guard !isDisabled, duration > 0 else { return }
                        guard w > 0 else {
                            isDragging = false
                            return
                        }
                        let final_ = clamped(value.location.x / w)
                        onSeek(Float(final_) * duration)
                        isDragging = false
                        NSCursor.arrow.set()
                    }
            )
        }
        .frame(height: 20)
    }
}

struct ContentView: View {
    @StateObject private var viewModel = EngineViewModel()
    @State private var createSeedText = ""
    @State private var createCount = 10
    @State private var showLibraryMenu = false
    
    var body: some View {
        ZStack {
            // 磨砂背景：填满整个窗口，避免底部空余
            // 使用极低透明度的白色而非 Color.clear，确保整个区域可被点击（hit-testable）
            Color.white.opacity(0.001)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(kBackgroundMaterial)
                .ignoresSafeArea(.all)
            
            // 左右滑动容器：播放界面 + 设置面板并排，overflow hidden
            GeometryReader { geo in
                let w = geo.size.width
                HStack(spacing: 0) {
                    // 左侧：播放界面
                    frontMainView
                        .frame(width: w, height: geo.size.height)
                    // 右侧：设置面板（紧挨着播放界面）
                    backSettingsView
                        .frame(width: w, height: geo.size.height)
                }
                .offset(x: showLibraryMenu ? -w : 0)
            }
            .clipped()
            
            // 箭头按钮放在最外层 overlay，不受 clipped 影响，可与标题栏 traffic light 垂直对齐
            // macOS hiddenTitleBar 下 content 有 top inset，需用负 offset 将按钮上移至 traffic light 高度
            .overlay(alignment: .topTrailing) {
                Group {
                    if showLibraryMenu {
                        Button(action: { showLibraryMenu = false }) {
                            Image(systemName: "chevron.right")
                                .font(.system(size: 16, weight: .medium))
                        }
                        .buttonStyle(CornerArrowButtonStyle())
                        .pointingHandCursor()
                    } else {
                        Button(action: { showLibraryMenu = true }) {
                            Image(systemName: "chevron.left")
                                .font(.system(size: 16, weight: .medium))
                        }
                        .buttonStyle(CornerArrowButtonStyle())
                        .pointingHandCursor()
                    }
                }
                .padding(.trailing, 10)
                .padding(.top, 5)
                .offset(y: -22)
            }
        }
        .contentShape(Rectangle())
        .frame(minWidth: kMinWindowWidth, minHeight: kMinWindowHeight)
        .modifier(WindowTransparencyModifier())
        .animation(.spring(response: 0.5, dampingFraction: 0.85), value: showLibraryMenu)
    }
    
    /// 左侧：播放界面
    private var frontMainView: some View {
        VStack(spacing: 0) {
            trackInfoSection
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(.horizontal, 16)
                .padding(.top, 16)
            progressSection
            playbackControls
                .padding(.top, 4)
                .padding(.bottom, 20)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    
    /// 右侧：设置面板（可滚动，保持主窗口尺寸不变）
    private var backSettingsView: some View {
        VStack(alignment: .leading, spacing: 0) {
            // 顶部留白，避免内容与最外层按钮重叠
            Color.clear.frame(height: 44)
            Divider()
                .background(Color.white.opacity(0.1))
            
            // 可滚动的核心内容区
            ScrollView(.vertical, showsIndicators: true) {
                libraryAndPlaylistPopover
                    .padding(25)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    
    /// 进度条区域（全宽进度条 + 左右时间标签）
    private var progressSection: some View {
        let duration = viewModel.currentTrackInfo?.duration ?? 0
        
        return VStack(spacing: 4) {
            PlaybackProgressBar(
                position: viewModel.position,
                duration: duration,
                isDisabled: viewModel.isTransitioning || viewModel.currentTrackId == 0,
                onSeek: { viewModel.seek(to: $0) }
            )
            
            HStack {
                Text(formatTime(viewModel.position))
                    .font(.caption.monospacedDigit())
                    .foregroundColor(.secondary)
                Spacer()
                Text(formatTime(duration))
                    .font(.caption.monospacedDigit())
                    .foregroundColor(.secondary)
            }
            .padding(.horizontal, 16)
        }
    }
    
    /// 播放控制按钮（固定在底部）
    private var playbackControls: some View {
        HStack(spacing: 10) {
            Button(action: { viewModel.previous() }) {
                Image(systemName: "backward.fill")
                    .font(.title)
            }
            .disabled(viewModel.currentTrackId == 0 || viewModel.isTransitioning)
            .buttonStyle(InteractiveButtonStyle())
            .pointingHandCursor()
            .accessibilityLabel("Previous track")
            
            Group {
                if viewModel.isTransitioning {
                    Image(systemName: "waveform.circle.fill")
                        .font(.system(size: 64))
                        .foregroundColor(.secondary)
                        .frame(width: 64, height: 64)
                        .accessibilityLabel("Transitioning")
                } else {
                    Button(action: { viewModel.togglePlayPause() }) {
                        Image(systemName: viewModel.isPlaying ? "pause.circle.fill" : "play.circle.fill")
                            .font(.system(size: 64))
                            .frame(width: 64, height: 64)
                    }
                    .buttonStyle(InteractiveButtonStyle())
                    .pointingHandCursor()
                    .accessibilityLabel(viewModel.isPlaying ? "Pause" : "Play")
                }
            }
            
            Button(action: { viewModel.next() }) {
                Image(systemName: "forward.fill")
                    .font(.title)
            }
            .disabled(viewModel.currentTrackId == 0 || viewModel.isTransitioning)
            .buttonStyle(InteractiveButtonStyle())
            .pointingHandCursor()
            .accessibilityLabel("Next track")
        }
    }
    
    /// Library 与 Playlist 操作（弹出菜单内容）
    private var libraryAndPlaylistPopover: some View {
        VStack(alignment: .leading, spacing: 16) {
            // Library Stats
            VStack(alignment: .leading, spacing: 8) {
                Text("Library Stats")
                    .font(.headline)
                HStack {
                    VStack(alignment: .leading) {
                        Text("\(viewModel.trackCount)")
                            .font(.title2)
                        Text("Tracks")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                }
                if viewModel.isScanning {
                    VStack(alignment: .leading, spacing: 4) {
                        ProgressView(value: Double(viewModel.scannedTracks), total: Double(viewModel.totalTracksToScan > 0 ? viewModel.totalTracksToScan : 100))
                        Text("Scanning: \(viewModel.scannedTracks) / \(viewModel.totalTracksToScan)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                Button("Scan Music Directory") {
                    viewModel.scanLibrary()
                }
                .disabled(viewModel.isScanning || viewModel.currentTrackId != 0)
                .buttonStyle(.borderedProminent)
                .pointingHandCursor()
                .help(viewModel.currentTrackId != 0 ? "Stop playback first to avoid file access conflicts" : "Scan a directory for music files")
            }
            
            Toggle("Mix 过渡效果", isOn: $viewModel.mixEnabled)
                .help(viewModel.mixEnabled ? "关闭后仅做元数据扫描，曲间硬切" : "开启后分析 BPM/调性，曲间平滑过渡")
            
            Divider()
            
            // Create Playlist
            VStack(alignment: .leading, spacing: 8) {
                Text("Create Playlist")
                    .font(.headline)
                HStack {
                    Text("Seed track ID:")
                    TextField("e.g. 5", text: $createSeedText)
                        .frame(width: 60)
                }
                HStack {
                    Text("Count:")
                    Stepper(value: $createCount, in: 5...50) {
                        Text("\(createCount)")
                    }
                }
                Button("Create & Play") {
                    if let seed = Int64(createSeedText), seed > 0 {
                        viewModel.createAndPlayPlaylist(seedTrackId: seed, count: createCount)
                        showLibraryMenu = false
                    } else {
                        viewModel.statusMessage = "Invalid seed track ID. Enter a positive number."
                    }
                }
                .disabled(viewModel.trackCount == 0 || viewModel.isTransitioning)
                .pointingHandCursor()
                if !viewModel.statusMessage.isEmpty && viewModel.statusMessage != "Ready" {
                    Text(viewModel.statusMessage)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(2)
                }
            }
        }
    }
    
    @ViewBuilder
    private var trackInfoSection: some View {
        if viewModel.currentTrackId != 0 {
            ZStack {
                // 当前曲目（过渡时淡出）
                if let info = viewModel.currentTrackInfo {
                    trackInfoContent(
                        info: info,
                        metadata: viewModel.currentTrackMetadata,
                        trackIndex: viewModel.currentTrackIndex,
                        totalCount: viewModel.totalPlaylistCount
                    )
                    .opacity(1.0 - viewModel.transitionProgress)
                }
                
                // 下一曲（过渡时淡入，叠加在上方）
                if viewModel.isTransitioning, let nextInfo = viewModel.nextTrackInfo {
                    trackInfoContent(
                        info: nextInfo,
                        metadata: viewModel.nextTrackMetadata,
                        trackIndex: viewModel.playlistTrackIds.firstIndex(of: nextInfo.id).map { $0 + 1 } ?? (viewModel.currentTrackIndex + 1),
                        totalCount: viewModel.totalPlaylistCount
                    )
                    .opacity(viewModel.transitionProgress)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            Text("No track playing")
                .foregroundColor(.secondary)
                .frame(maxWidth: .infinity, minHeight: 120)
        }
    }
    
    @ViewBuilder
    private func trackInfoContent(
        info: TrackInfo,
        metadata: TrackMetadata?,
        trackIndex: Int,
        totalCount: Int
    ) -> some View {
        let artistName = metadata?.artist ?? ""
        let albumName = metadata?.album ?? ""
        
        VStack(spacing: 12) {
            // 封面
            if let artwork = metadata?.artwork {
                Image(nsImage: artwork)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
                    .frame(width: 160, height: 160)
                    .cornerRadius(10)
            } else {
                RoundedRectangle(cornerRadius: 10)
                    .fill(Color.gray.opacity(0.3))
                    .frame(width: 160, height: 160)
                    .overlay(
                        Image(systemName: "music.note")
                            .font(.system(size: 50))
                            .foregroundColor(.gray)
                    )
            }
            
            // 曲目名称（固定一行，超出省略）
            Text(displayTitleForInfo(info, metadata: metadata))
                .font(.title2)
                .fontWeight(.medium)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(maxWidth: .infinity, alignment: .center)
            
            // 艺术家（固定一行高度，无内容时占位不可见）
            Text(artistName.isEmpty ? " " : artistName)
                .font(.subheadline)
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(maxWidth: .infinity, alignment: .center)
                .opacity(artistName.isEmpty ? 0 : 1)
            
            // 专辑（固定一行高度，无内容时占位不可见）
            Text(albumName.isEmpty ? " " : albumName)
                .font(.caption)
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(maxWidth: .infinity, alignment: .center)
                .opacity(albumName.isEmpty ? 0 : 1)
            
            // 第几首 / 共几首（始终占位）
            Text(totalCount > 0 ? "\(trackIndex) / \(totalCount)" : " ")
                .font(.caption)
                .foregroundColor(.secondary)
                .opacity(totalCount > 0 ? 1 : 0)
        }
        .frame(maxWidth: .infinity)
    }
    
    private func displayTitleForInfo(_ info: TrackInfo, metadata: TrackMetadata?) -> String {
        if let title = metadata?.title, !title.isEmpty {
            return title
        }
        return URL(fileURLWithPath: info.path).deletingPathExtension().lastPathComponent
    }
    
    private func formatTime(_ seconds: Float) -> String {
        let m = Int(seconds) / 60
        let s = Int(seconds) % 60
        return String(format: "%d:%02d", m, s)
    }
}
