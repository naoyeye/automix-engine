import Foundation
import Combine
import Automix
import AppKit

@MainActor
class EngineViewModel: ObservableObject {
    @Published var isPlaying: Bool = false
    @Published var currentTrackId: Int64 = 0
    @Published var position: Float = 0.0
    @Published var statusMessage: String = "Ready"
    @Published var trackCount: Int = 0
    @Published var scannedTracks: Int = 0
    @Published var totalTracksToScan: Int = 0
    @Published var isScanning: Bool = false
    
    private var engine: AutoMixEngine?
    private var cancellables = Set<AnyCancellable>()
    
    init() {
        setupEngine()
    }
    
    func setupEngine() {
        let dbPath = FileManager.default.temporaryDirectory.appendingPathComponent("automix.db").path
        do {
            engine = try AutoMixEngine(dbPath: dbPath)
            statusMessage = "Engine initialized at \(dbPath)"
            
            engine?.statusPublisher
                .receive(on: RunLoop.main)
                .sink { [weak self] status in
                    self?.updateStatus(status)
                }
                .store(in: &cancellables)
            
            // Start audio engine
            try engine?.startAudio()
                
        } catch {
            statusMessage = "Failed to initialize engine: \(error)"
        }
    }
    
    func updateStatus(_ status: AutoMixStatus) {
        // Assuming AutoMixPlaybackState is imported as C enum
        // We need to check the raw value or enum case if mapped
        // Since it's C enum, it might be Int32 or similar in Swift unless NS_ENUM is used
        // But let's assume it works as imported enum for now.
        // If AUTOMIX_STATE_PLAYING is 1
        self.isPlaying = (status.state == .playing)
        self.currentTrackId = status.currentTrackId
        self.position = status.position
    }
    
    func scanLibrary() {
        guard let engine = engine else { return }
        
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.message = "Select Music Directory"
        
        panel.begin { [weak self] response in
            if response == .OK, let url = panel.url {
                self?.startScanning(path: url.path)
            }
        }
    }
    
    private func startScanning(path: String) {
        guard let engine = engine else { return }
        
        isScanning = true
        scannedTracks = 0
        totalTracksToScan = 0
        statusMessage = "Scanning..."
        
        Task {
            do {
                for try await (file, processed, total) in engine.scanProgressStream(musicDir: path, recursive: true) {
                    await MainActor.run {
                        self.scannedTracks = processed
                        self.totalTracksToScan = total
                        self.statusMessage = "Scanning: \(processed)/\(total) - \(URL(fileURLWithPath: file).lastPathComponent)"
                    }
                }
                let trackCount = engine.trackCount()
                await MainActor.run {
                    self.isScanning = false
                    self.statusMessage = "Scan complete. Total tracks: \(self.scannedTracks)"
                    self.trackCount = trackCount
                }
            } catch {
                await MainActor.run {
                    self.isScanning = false
                    self.statusMessage = "Scan failed: \(error)"
                }
            }
        }
    }
    
    func togglePlayPause() {
        guard let engine = engine else { return }
        do {
            if isPlaying {
                try engine.pause()
            } else {
                // If stopped, we might need to play a playlist first
                // For demo, let's try to generate a playlist if none playing
                if engine.currentTrackId == 0 {
                    // Generate random playlist
                     // We need a seed track. Let's pick the first one if available.
                    // Since we don't have a way to get all tracks easily yet (except search),
                    // let's search for "%" to get all.
                    let allTracks = try engine.searchTracks(pattern: "%")
                    if let seed = allTracks.first {
                        let playlist = try engine.generatePlaylist(seedTrackId: seed, count: 10)
                        try engine.play(playlist: playlist)
                    } else {
                        statusMessage = "No tracks to play. Scan library first."
                    }
                } else {
                    try engine.resume()
                }
            }
        } catch {
            statusMessage = "Playback error: \(error)"
        }
    }
    
    func next() {
        try? engine?.next()
    }
    
    func previous() {
        try? engine?.previous()
    }
}
