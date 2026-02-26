import SwiftUI
import Automix
import AppKit

/// 配置窗口为透明，以便磨砂背景能透出桌面
private struct WindowTransparencyModifier: ViewModifier {
    func body(content: Content) -> some View {
        content.background(WindowConfigurator())
    }
}

private class WindowConfiguratorView: NSView {
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let window = window else { return }
        window.isOpaque = false
        window.backgroundColor = .clear
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
                    if NSCursor.current != NSCursor.pointingHand {
                        NSCursor.pointingHand.push()
                    }
                case .ended:
                    NSCursor.pop()
                }
            }
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

struct ContentView: View {
    @StateObject private var viewModel = EngineViewModel()
    @State private var createSeedText = "5"
    @State private var createCount = 10
    @State private var showLibraryMenu = false
    
    var body: some View {
        ZStack {
            // 磨砂背景：填满整个窗口，避免底部空余
            Color.clear
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(.ultraThinMaterial)
                .ignoresSafeArea(.all)
            
            VStack(spacing: 0) {
                // Track Info（上方，占据剩余空间）
                trackInfoSection
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                
                // Playback Controls（固定在底部）
                playbackControls
                    .padding(.vertical, 20)
            }
            .frame(minWidth: 580, minHeight: 500)
            .padding()
            
        }
        .frame(minWidth: 580, minHeight: 500)
        .overlay(alignment: .topTrailing) {
            Button(action: { showLibraryMenu.toggle() }) {
                Image(systemName: "ellipsis")
                    .font(.system(size: 16, weight: .medium))
            }
            .buttonStyle(InteractiveButtonStyle())
            .pointingHandCursor()
            .popover(isPresented: $showLibraryMenu, arrowEdge: .top) {
                libraryAndPlaylistPopover
                    .frame(width: 280)
                    .padding()
            }
            .padding(.trailing, 25)
            .padding(.top, -7)
        }
        .modifier(WindowTransparencyModifier())
    }
    
    /// 播放控制按钮（固定在底部）
    private var playbackControls: some View {
        HStack(spacing: 10) {
            Button(action: { viewModel.previous() }) {
                Image(systemName: "backward.fill")
                    .font(.title)
            }
            .disabled(viewModel.currentTrackId == 0)
            .buttonStyle(InteractiveButtonStyle())
            .pointingHandCursor()
            
            Group {
                if viewModel.isTransitioning {
                    Image(systemName: "waveform.circle.fill")
                        .font(.system(size: 64))
                        .foregroundColor(.secondary)
                        .frame(width: 64, height: 64)
                } else {
                    Button(action: { viewModel.togglePlayPause() }) {
                        Image(systemName: viewModel.isPlaying ? "pause.circle.fill" : "play.circle.fill")
                            .font(.system(size: 64))
                            .frame(width: 64, height: 64)
                    }
                    .buttonStyle(InteractiveButtonStyle())
                    .pointingHandCursor()
                }
            }
            
            Button(action: { viewModel.next() }) {
                Image(systemName: "forward.fill")
                    .font(.title)
            }
            .disabled(viewModel.currentTrackId == 0)
            .buttonStyle(InteractiveButtonStyle())
            .pointingHandCursor()
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
                .disabled(viewModel.isScanning)
                .buttonStyle(.borderedProminent)
                .pointingHandCursor()
            }
            
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
                    if let seed = Int64(createSeedText) {
                        viewModel.createAndPlayPlaylist(seedTrackId: seed, count: createCount)
                        showLibraryMenu = false
                    }
                }
                .disabled(viewModel.trackCount == 0 || viewModel.isPlaying)
                .pointingHandCursor()
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
                        position: viewModel.position,
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
                        position: 0,
                        trackIndex: viewModel.currentTrackIndex + 1,
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
        position: Float,
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
            
            // 进度与时长
            HStack(spacing: 8) {
                Text(formatTime(position))
                    .font(.body.monospacedDigit())
                Text("/")
                Text(formatTime(info.duration))
                    .font(.body.monospacedDigit())
            }
            .foregroundColor(.secondary)
            
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
