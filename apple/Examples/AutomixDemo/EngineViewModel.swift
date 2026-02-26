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
    
    /// 当前播放曲目信息（用于 UI 显示）
    @Published var currentTrackInfo: TrackInfo?
    /// 当前曲目从音频文件提取的元数据（艺术家、专辑、封面等）
    @Published var currentTrackMetadata: TrackMetadata?
    /// 当前播放列表中的曲目 ID 列表（用于计算「第几首 / 共几首」）
    @Published var playlistTrackIds: [Int64] = []
    /// 生成播放列表时请求的曲目数（getTrackIDs 失败时用于 fallback 的总数）
    @Published var requestedPlaylistCount: Int = 0
    
    /// 当前是第几首（1-based）
    var currentTrackIndex: Int {
        guard currentTrackId != 0, let idx = playlistTrackIds.firstIndex(of: currentTrackId) else { return 0 }
        return idx + 1
    }
    
    /// 播放列表总曲数：优先使用请求数量，其次用实际列表长度，取较大值以避免逐首递增
    var totalPlaylistCount: Int {
        max(requestedPlaylistCount, playlistTrackIds.count)
    }
    
    private var engine: AutoMixEngine?
    private var cancellables = Set<AnyCancellable>()
    private var pollTimerCancellable: AnyCancellable?
    private var metadataTask: Task<Void, Never>?
    
    init() {
        setupEngine()
    }
    
    /// 返回持久化数据库路径，并尝试从临时目录迁移已有数据库
    private static func persistentDatabasePath() -> String {
        let fileManager = FileManager.default
        guard let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            return fileManager.temporaryDirectory.appendingPathComponent("automix.db").path
        }
        let automixDir = appSupport.appendingPathComponent("AutomixDemo", isDirectory: true)
        try? fileManager.createDirectory(at: automixDir, withIntermediateDirectories: true)
        let destPath = automixDir.appendingPathComponent("automix.db").path
        
        // 1. 优先从项目根目录（当前工作目录）迁移，便于与 automix-scan/playlist 共用
        migrateFromProjectRootIfNeeded(fileManager: fileManager, destination: destPath, destDir: automixDir)
        // 2. 尝试从标准临时目录迁移已有数据库
        migrateFromTempIfNeeded(fileManager: fileManager, destination: destPath)
        
        return destPath
    }
    
    /// 从项目根目录（当前工作目录）迁移 automix.db 及 SQLite 相关文件
    private static func migrateFromProjectRootIfNeeded(fileManager: FileManager, destination: String, destDir: URL) {
        let projectRoot = URL(fileURLWithPath: fileManager.currentDirectoryPath)
        let srcDb = projectRoot.appendingPathComponent("automix.db")
        guard migrateFile(from: srcDb.path, to: destination, fileManager: fileManager) else { return }
        // 迁移 SQLite 辅助文件（-journal, -shm, -wal）
        let auxSuffixes = ["-journal", "-shm", "-wal"]
        for suffix in auxSuffixes {
            let srcAux = projectRoot.appendingPathComponent("automix.db\(suffix)")
            let dstAux = destDir.appendingPathComponent("automix.db\(suffix)")
            if fileManager.fileExists(atPath: srcAux.path) {
                try? fileManager.removeItem(atPath: dstAux.path)
                try? fileManager.copyItem(atPath: srcAux.path, toPath: dstAux.path)
            }
        }
    }
    
    /// 在标准临时目录中查找 automix.db，若存在且非空则复制到目标路径（含辅助文件）
    private static func migrateFromTempIfNeeded(fileManager: FileManager, destination: String) {
        let tempDir = fileManager.temporaryDirectory
        let tempDb = tempDir.appendingPathComponent("automix.db")
        guard migrateFile(from: tempDb.path, to: destination, fileManager: fileManager) else { return }
        let destDir = URL(fileURLWithPath: destination).deletingLastPathComponent()
        let auxSuffixes = ["-journal", "-shm", "-wal"]
        for suffix in auxSuffixes {
            let srcAux = tempDir.appendingPathComponent("automix.db\(suffix)")
            let dstAux = destDir.appendingPathComponent("automix.db\(suffix)")
            if fileManager.fileExists(atPath: srcAux.path) {
                try? fileManager.removeItem(atPath: dstAux.path)
                try? fileManager.copyItem(atPath: srcAux.path, toPath: dstAux.path)
            }
        }
    }
    
    /// 若源文件存在且非空，则复制到目标；若目标已有有效数据则不覆盖。返回是否已迁移。
    private static func migrateFile(from src: String, to dst: String, fileManager: FileManager) -> Bool {
        guard fileManager.fileExists(atPath: src) else { return false }
        guard let attrs = try? fileManager.attributesOfItem(atPath: src),
              let size = attrs[.size] as? Int64, size > 0 else { return false }
        if fileManager.fileExists(atPath: dst),
           let dstAttrs = try? fileManager.attributesOfItem(atPath: dst),
           let dstSize = dstAttrs[.size] as? Int64, dstSize > 0 {
            return false
        }
        if fileManager.fileExists(atPath: dst) { try? fileManager.removeItem(atPath: dst) }
        guard (try? fileManager.copyItem(atPath: src, toPath: dst)) != nil else { return false }
        return fileManager.fileExists(atPath: dst)
    }
    
    func setupEngine() {
        let dbPath = Self.persistentDatabasePath()
        do {
            engine = try AutoMixEngine(dbPath: dbPath)
            statusMessage = "Engine initialized at \(dbPath)"
            trackCount = engine?.trackCount() ?? 0
            
            engine?.statusPublisher
                .receive(on: RunLoop.main)
                .sink { [weak self] status in
                    self?.updateStatus(status)
                }
                .store(in: &cancellables)
            
            // Start audio engine
            try engine?.startAudio()
            
            // 启动 poll 定时器，驱动曲目切换、预加载等（约 20ms 一次）
            startPollTimer()
                
        } catch {
            statusMessage = "Failed to initialize engine: \(error)"
        }
    }
    
    private func startPollTimer() {
        stopPollTimer()
        pollTimerCancellable = Timer.publish(every: 0.02, tolerance: 0.005, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                guard let self = self, let engine = self.engine else { return }
                engine.poll()
                if self.isPlaying, engine.state != .transitioning {
                    self.position = engine.position
                }
            }
    }
    
    private func stopPollTimer() {
        pollTimerCancellable?.cancel()
        pollTimerCancellable = nil
    }
    
    func updateStatus(_ status: AutoMixStatus) {
        self.isPlaying = (status.state == .playing)
        
        let newTrackId = status.currentTrackId
        let trackChanged = newTrackId != self.currentTrackId
        self.currentTrackId = newTrackId
        
        if newTrackId != 0, trackChanged {
            refreshCurrentTrackInfo(trackId: newTrackId)
            if !playlistTrackIds.contains(newTrackId) {
                playlistTrackIds.append(newTrackId)
            }
        } else if newTrackId == 0, status.state == .stopped {
            metadataTask?.cancel()
            metadataTask = nil
            currentTrackInfo = nil
            currentTrackMetadata = nil
            self.position = 0
        }
    }
    
    private func refreshCurrentTrackInfo(trackId: Int64) {
        guard let engine = engine else { return }
        
        metadataTask?.cancel()
        metadataTask = nil
        currentTrackMetadata = nil
        
        do {
            currentTrackInfo = try engine.trackInfo(id: trackId)
            if let info = currentTrackInfo {
                let path = info.path
                metadataTask = Task.detached { [weak self] in
                    let metadata = await TrackMetadataLoader.loadMetadata(from: path)
                    guard !Task.isCancelled else { return }
                    await MainActor.run {
                        guard let self = self, self.currentTrackInfo?.id == trackId else { return }
                        self.currentTrackMetadata = metadata
                    }
                }
            }
        } catch {
            currentTrackInfo = nil
            currentTrackMetadata = nil
        }
    }
    
    func scanLibrary() {
        guard engine != nil else { return }
        
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
                    let allTracks = try engine.searchTracks(pattern: "%")
                    if let seed = allTracks.first {
                        let playlistCount = 10
                        let playlist = try engine.generatePlaylist(seedTrackId: seed, count: playlistCount)
                        requestedPlaylistCount = playlistCount
                        do {
                            playlistTrackIds = try playlist.getTrackIDs()
                        } catch {
                            playlistTrackIds = []
                            requestedPlaylistCount = 0
                        }
                        try engine.play(playlist: playlist)
                        refreshCurrentTrackInfo(trackId: engine.currentTrackId)
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
        guard let engine = engine else { return }
        let savedTrackId = currentTrackId
        let savedPosition = position
        
        if currentTrackIndex > 0, currentTrackIndex < playlistTrackIds.count {
            let nextId = playlistTrackIds[currentTrackIndex]
            currentTrackId = nextId
            position = 0
            refreshCurrentTrackInfo(trackId: nextId)
        }
        do {
            try engine.next()
        } catch {
            currentTrackId = savedTrackId
            position = savedPosition
            refreshCurrentTrackInfo(trackId: savedTrackId)
            statusMessage = "Next failed: \(error)"
        }
    }
    
    func previous() {
        guard let engine = engine else { return }
        let savedTrackId = currentTrackId
        let savedPosition = position
        
        if currentTrackIndex > 1, currentTrackIndex - 2 < playlistTrackIds.count {
            let prevId = playlistTrackIds[currentTrackIndex - 2]
            currentTrackId = prevId
            position = 0
            refreshCurrentTrackInfo(trackId: prevId)
        }
        do {
            try engine.previous()
        } catch {
            currentTrackId = savedTrackId
            position = savedPosition
            refreshCurrentTrackInfo(trackId: savedTrackId)
            statusMessage = "Previous failed: \(error)"
        }
    }
}
