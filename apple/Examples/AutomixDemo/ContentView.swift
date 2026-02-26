import SwiftUI
import Automix

struct ContentView: View {
    @StateObject private var viewModel = EngineViewModel()
    
    var body: some View {
        VStack(spacing: 20) {
            Text("AutoMix Demo")
                .font(.largeTitle)
            
            // Status
            Text(viewModel.statusMessage)
                .font(.headline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)
            
            // Playback Controls
            HStack(spacing: 30) {
                Button(action: { viewModel.previous() }) {
                    Image(systemName: "backward.fill")
                        .font(.title)
                }
                .disabled(viewModel.currentTrackId == -1)
                
                Button(action: { viewModel.togglePlayPause() }) {
                    Image(systemName: viewModel.isPlaying ? "pause.circle.fill" : "play.circle.fill")
                        .font(.system(size: 64))
                }
                
                Button(action: { viewModel.next() }) {
                    Image(systemName: "forward.fill")
                        .font(.title)
                }
                .disabled(viewModel.currentTrackId == -1)
            }
            .padding()
            
            // Track Info
            VStack(spacing: 8) {
                if viewModel.currentTrackId != -1 {
                    Text("Track ID: \(viewModel.currentTrackId)")
                        .font(.title2)
                    Text("Position: \(String(format: "%.1f", viewModel.position))s")
                        .font(.body.monospacedDigit())
                } else {
                    Text("No track playing")
                        .foregroundColor(.secondary)
                }
            }
            .frame(height: 80)
            
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
            }
            .padding()
        }
        .frame(minWidth: 400, minHeight: 600)
        .padding()
    }
}
