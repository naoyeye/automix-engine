import SwiftUI
import Automix

struct ContentView: View {
    @StateObject private var viewModel = EngineViewModel()
    @State private var createSeedText = "5"
    @State private var createCount = 10
    
    var body: some View {
        VStack(spacing: 20) {
            // Text("AutoMix Demo")
            //     .font(.largeTitle)
            
            // // Status
            // Text(viewModel.statusMessage)
            //     .font(.headline)
            //     .foregroundColor(.secondary)
            //     .multilineTextAlignment(.center)
            //     .padding(.horizontal)
            
            // Playback Controls
            HStack(spacing: 10) {
                Button(action: { viewModel.previous() }) {
                    Image(systemName: "backward.fill")
                        .font(.title)
                }
                .disabled(viewModel.currentTrackId == 0)
                
                Button(action: { viewModel.togglePlayPause() }) {
                    Image(systemName: viewModel.isPlaying ? "pause.circle.fill" : "play.circle.fill")
                        .font(.system(size: 64))
                }
                
                Button(action: { viewModel.next() }) {
                    Image(systemName: "forward.fill")
                        .font(.title)
                }
                .disabled(viewModel.currentTrackId == 0)
            }
            .padding()
            
            // Track Info
            trackInfoSection
            
            Divider()
            
            // Library Controls
            VStack(spacing: 12) {
                Text("Library Stats")
                    .font(.headline)
                
                HStack {
                    VStack {
                        Text("\(viewModel.trackCount)")
                            .font(.title)
                        Text("Tracks")
                            .font(.caption)
                    }
                }
                
                if viewModel.isScanning {
                    VStack {
                        ProgressView(value: Double(viewModel.scannedTracks), total: Double(viewModel.totalTracksToScan > 0 ? viewModel.totalTracksToScan : 100))
                        Text("Scanning: \(viewModel.scannedTracks) / \(viewModel.totalTracksToScan)")
                            .font(.caption)
                    }
                    .padding()
                }
                
                Button("Scan Music Directory") {
                    viewModel.scanLibrary()
                }
                .disabled(viewModel.isScanning)
                .buttonStyle(.borderedProminent)

                // 创建播放列表（与 CLI automix-playlist 一致）
                GroupBox {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("Create Playlist")
                            .font(.headline)
                        HStack {
                            Text("Seed track ID:")
                            TextField("e.g. 5", text: $createSeedText)
                                .frame(width: 60)
                            Text("Count:")
                            Stepper(value: $createCount, in: 5...50) {
                                Text("\(createCount)")
                            }
                        }
                        Button("Create & Play") {
                            if let seed = Int64(createSeedText) {
                                viewModel.createAndPlayPlaylist(seedTrackId: seed, count: createCount)
                            }
                        }
                        .disabled(viewModel.trackCount == 0 || viewModel.isPlaying)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .padding()
        }
        .frame(minWidth: 400, minHeight: 600)
        .padding()
    }
    
    @ViewBuilder
    private var trackInfoSection: some View {
        if viewModel.currentTrackId != 0, let info = viewModel.currentTrackInfo {
            VStack(spacing: 12) {
                // 封面
                if let artwork = viewModel.currentTrackMetadata?.artwork {
                    Image(nsImage: artwork)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(width: 120, height: 120)
                        .cornerRadius(8)
                } else {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color.gray.opacity(0.3))
                        .frame(width: 120, height: 120)
                        .overlay(
                            Image(systemName: "music.note")
                                .font(.system(size: 40))
                                .foregroundColor(.gray)
                        )
                }
                
                // 曲目名称
                Text(displayTitle(info: info))
                    .font(.title2)
                    .fontWeight(.medium)
                    .lineLimit(1)
                
                // 艺术家、专辑（若有）
                if let artist = viewModel.currentTrackMetadata?.artist, !artist.isEmpty {
                    Text(artist)
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                }
                if let album = viewModel.currentTrackMetadata?.album, !album.isEmpty {
                    Text(album)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                
                // 进度与时长
                HStack(spacing: 8) {
                    Text(formatTime(viewModel.position))
                        .font(.body.monospacedDigit())
                    Text("/")
                    Text(formatTime(info.duration))
                        .font(.body.monospacedDigit())
                }
                .foregroundColor(.secondary)
                
                // 第几首 / 共几首
                if viewModel.totalPlaylistCount > 0 {
                    Text("\(viewModel.currentTrackIndex) / \(viewModel.totalPlaylistCount)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .frame(minHeight: 200)
        } else {
            Text("No track playing")
                .foregroundColor(.secondary)
                .frame(height: 80)
        }
    }
    
    private func displayTitle(info: TrackInfo) -> String {
        if let title = viewModel.currentTrackMetadata?.title, !title.isEmpty {
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
