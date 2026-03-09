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
    
    /// 是否处于 mix 过渡中（此时播放按钮应显示特殊状态且不可点击）
    @Published var isTransitioning: Bool = false
    /// 过渡动画进度 0...1，用于当前曲目淡出、下一曲淡入
    @Published var transitionProgress: Double = 0
    
    /// 当前播放曲目信息（用于 UI 显示）
    @Published var currentTrackInfo: TrackInfo?
    /// 当前曲目从音频文件提取的元数据（艺术家、专辑、封面等）
    @Published var currentTrackMetadata: TrackMetadata?
    /// 下一曲信息（过渡期间显示，叠加在当前曲之上）
    @Published var nextTrackInfo: TrackInfo?
    @Published var nextTrackMetadata: TrackMetadata?
    /// 当前播放列表中的曲目 ID 列表（用于计算「第几首 / 共几首」）
    @Published var playlistTrackIds: [Int64] = []
    /// 生成播放列表时请求的曲目数（getTrackIDs 失败时用于 fallback 的总数）
    @Published var requestedPlaylistCount: Int = 0
    
    /// 是否启用 mix 过渡效果；关闭时扫描只做元数据、播放使用硬切
    @Published var mixEnabled: Bool {
        didSet {
            UserDefaults.standard.set(mixEnabled, forKey: "automix_mixEnabled")
            applyTransitionConfig()
        }
    }
    
    private var acoustidApiKey: String?
    private var scanStartTime: Date?
    
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
    private var nextMetadataTask: Task<Void, Never>?
    private var transitionProgressCancellable: AnyCancellable?
    
    init() {
        mixEnabled = UserDefaults.standard.object(forKey: "automix_mixEnabled") as? Bool ?? true
        setupEngine()
    }
    
    deinit {
        metadataTask?.cancel()
        nextMetadataTask?.cancel()
        transitionProgressCancellable?.cancel()
        pollTimerCancellable?.cancel()
        pollTimerCancellable = nil
    }
    
    /// 返回持久化数据库路径。优先级：AUTOMIX_DB 环境变量 > Application Support/Automix/automix.db，
    /// 与 CLI 工具共用同一默认路径。会尝试从临时目录迁移已有数据库。
    private static func persistentDatabasePath() -> String {
        if let envDb = ProcessInfo.processInfo.environment["AUTOMIX_DB"], !envDb.isEmpty {
            return envDb
        }
        let fileManager = FileManager.default
        guard let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else {
            return fileManager.temporaryDirectory.appendingPathComponent("automix.db").path
        }
        let automixDir = appSupport.appendingPathComponent("Automix", isDirectory: true)
        try? fileManager.createDirectory(at: automixDir, withIntermediateDirectories: true)
        return automixDir.appendingPathComponent("automix.db").path
    }

    /// Mix 关闭时：seed 放首位，其余随机，保证 Create & Play 的 seed 生效
    private static func _shuffledWithSeedFirst(allTracks: [Int64], seedTrackId: Int64, count: Int) -> [Int64] {
        let others = allTracks.filter { $0 != seedTrackId }
        let shuffled = others.shuffled()
        let head = allTracks.contains(seedTrackId) ? [seedTrackId] : []
        return head + Array(shuffled.prefix(max(0, count - head.count)))
    }

    private static func keysPath() -> String {
        let fileManager = FileManager.default
        if let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            let automixDir = appSupport.appendingPathComponent("AutomixDemo", isDirectory: true)
            let path = automixDir.appendingPathComponent("keys.json").path
            if fileManager.fileExists(atPath: path) { return path }
        }
        
        let localPath = URL(fileURLWithPath: fileManager.currentDirectoryPath).appendingPathComponent("keys.json").path
        if fileManager.fileExists(atPath: localPath) { return localPath }
        
        return Bundle.main.path(forResource: "keys", ofType: "json") ?? ""
    }
    
    private func loadKeys() {
        let path = Self.keysPath()
        guard let data = try? Data(contentsOf: URL(fileURLWithPath: path)),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: String] else {
            return
        }
        acoustidApiKey = json["apikey"]
    }

    /// 共享播放列表文件路径（与 CLI 一致，与数据库同目录）
    private static func sharedPlaylistPathFor(dbPath: String) -> String {
        let url = URL(fileURLWithPath: dbPath)
        return url.deletingLastPathComponent().appendingPathComponent("automix_playlist.txt").path
    }

    /// 从共享文件加载播放列表（CLI 生成后 Demo 可读取）
    private static func loadSharedPlaylist(dbPath: String) -> [Int64]? {
        let path = sharedPlaylistPathFor(dbPath: dbPath)
        guard let content = try? String(contentsOfFile: path, encoding: .utf8) else { return nil }
        let ids = content
            .split(separator: "\n")
            .compactMap { line -> Int64? in Int64(line.trimmingCharacters(in: .whitespaces)) }
        return ids.isEmpty ? nil : ids
    }

    /// 保存播放列表到共享文件（Demo 创建后 CLI 也可读取）
    private static func saveSharedPlaylist(dbPath: String, trackIds: [Int64]) {
        let path = sharedPlaylistPathFor(dbPath: dbPath)
        let content = trackIds.map { String($0) }.joined(separator: "\n")
        try? content.write(toFile: path, atomically: true, encoding: .utf8)
    }
    
    func setupEngine() {
        loadKeys()
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
            
            applyTransitionConfig()
                
        } catch {
            statusMessage = "Failed to initialize engine: \(error)"
        }
    }
    
    private func applyTransitionConfig() {
        guard let engine = engine else { return }
        var config = TransitionConfig()
        config.enableTransitions = mixEnabled
        engine.setTransitionConfig(config)
    }
    
    private func startPollTimer() {
        stopPollTimer()
        pollTimerCancellable = Timer.publish(every: 0.02, tolerance: 0.005, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                guard let self = self, let engine = self.engine else { return }
                guard engine.state != .stopped else { return }
                engine.poll()
                // 过渡期间不更新 position，避免显示混乱
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
        // 过渡中音乐仍在播放，UI 上应显示「暂停按钮」（即播放状态）
        self.isPlaying = (status.state == .playing || status.state == .transitioning)
        self.isTransitioning = (status.state == .transitioning)
        
        let newTrackId = status.currentTrackId
        let trackChanged = newTrackId != self.currentTrackId
        self.currentTrackId = newTrackId
        
        if status.state == .transitioning {
            // 过渡期间：加载下一曲信息，启动视觉过渡动画；不更新 currentTrackInfo
            if status.nextTrackId != 0, status.nextTrackId != nextTrackInfo?.id {
                refreshNextTrackInfo(trackId: status.nextTrackId)
            }
            startTransitionProgressAnimation()
        } else {
            stopTransitionProgressAnimation()
            if status.state == .playing, trackChanged {
                // 过渡完成：将下一曲提升为当前曲（避免闪烁）
                if let next = nextTrackInfo, next.id == newTrackId {
                    currentTrackInfo = next
                    currentTrackMetadata = nextTrackMetadata
                    nextTrackInfo = nil
                    nextTrackMetadata = nil
                    nextMetadataTask?.cancel()
                    nextMetadataTask = nil
                } else {
                    refreshCurrentTrackInfo(trackId: newTrackId)
                }
                if !playlistTrackIds.contains(newTrackId) {
                    playlistTrackIds.append(newTrackId)
                }
            } else if newTrackId != 0, trackChanged {
                nextTrackInfo = nil
                nextTrackMetadata = nil
                nextMetadataTask?.cancel()
                nextMetadataTask = nil
                refreshCurrentTrackInfo(trackId: newTrackId)
                if !playlistTrackIds.contains(newTrackId) {
                    playlistTrackIds.append(newTrackId)
                }
            } else if newTrackId == 0, status.state == .stopped {
                metadataTask?.cancel()
                metadataTask = nil
                nextMetadataTask?.cancel()
                nextMetadataTask = nil
                currentTrackInfo = nil
                currentTrackMetadata = nil
                nextTrackInfo = nil
                nextTrackMetadata = nil
                self.position = 0
                transitionProgress = 0
            }
        }
    }
    
    /// 过渡动画进度：约 8 秒（与默认 crossfade 时长一致）
    private static let transitionAnimationDuration: TimeInterval = 8.0
    
    private func startTransitionProgressAnimation() {
        guard transitionProgressCancellable == nil else { return }
        let startProgress = transitionProgress
        let startTime = Date()
        transitionProgressCancellable = Timer.publish(every: 0.05, tolerance: 0.01, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                guard let self = self else { return }
                let elapsed = Date().timeIntervalSince(startTime)
                let delta = elapsed / Self.transitionAnimationDuration
                self.transitionProgress = min(1.0, startProgress + delta)
                if self.transitionProgress >= 1.0 {
                    self.stopTransitionProgressAnimation()
                }
            }
    }
    
    private func stopTransitionProgressAnimation() {
        transitionProgressCancellable?.cancel()
        transitionProgressCancellable = nil
        if !isTransitioning {
            transitionProgress = 0
        }
    }
    
    private func refreshNextTrackInfo(trackId: Int64) {
        guard let engine = engine else { return }
        nextMetadataTask?.cancel()
        nextMetadataTask = nil
        nextTrackMetadata = nil
        do {
            nextTrackInfo = try engine.trackInfo(id: trackId)
            if let info = nextTrackInfo {
                let path = info.path
                nextMetadataTask = Task.detached { [weak self] in
                    let apiKey = await self?.acoustidApiKey
                    let metadata = await MetadataService.loadMetadata(for: trackId, path: path, engine: engine, apiKey: apiKey)
                    guard !Task.isCancelled else { return }
                    let viewModel = self
                    await MainActor.run {
                        guard let viewModel = viewModel else { return }
                        // 若已 promote（下一曲即当前曲），则更新 currentTrackMetadata
                        if viewModel.currentTrackId == trackId {
                            viewModel.currentTrackMetadata = metadata
                        } else if viewModel.nextTrackInfo?.id == trackId {
                            viewModel.nextTrackMetadata = metadata
                        }
                    }
                }
            }
        } catch {
            nextTrackInfo = nil
            nextTrackMetadata = nil
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
                    let apiKey = await self?.acoustidApiKey
                    let metadata = await MetadataService.loadMetadata(for: trackId, path: path, engine: engine, apiKey: apiKey)
                    guard !Task.isCancelled else { return }
                    let viewModel = self
                    await MainActor.run {
                        guard let viewModel = viewModel, viewModel.currentTrackInfo?.id == trackId else { return }
                        viewModel.currentTrackMetadata = metadata
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
        scanStartTime = Date()
        
        Task {
            do {
                for try await (file, processed, total) in engine.scanProgressStream(musicDir: path, recursive: true, metadataOnly: !mixEnabled) {
                    await MainActor.run {
                        self.scannedTracks = processed
                        self.totalTracksToScan = total
                        self.statusMessage = "Scanning: \(processed)/\(total) - \(URL(fileURLWithPath: file).lastPathComponent)"
                    }
                }
                let trackCount = engine.trackCount()
                let apiKey = self.acoustidApiKey
                let startTime = self.scanStartTime
                
                // Fetch metadata for newly scanned tracks in bounded batches
                if let startTime = startTime, let tracks = try? engine.searchTracks(pattern: "%") {
                    let scanLookbackSeconds: Int64 = 60
                    let cutoffTimestamp = Int64(startTime.timeIntervalSince1970) - scanLookbackSeconds
                    var pendingRequests: [(id: Int64, path: String)] = []
                    for trackId in tracks {
                        if let info = try? engine.trackInfo(id: trackId),
                           info.analyzedAt >= cutoffTimestamp {
                            pendingRequests.append((id: trackId, path: info.path))
                        }
                    }

                    let batchSize = 10
                    var index = 0
                    while index < pendingRequests.count {
                        let end = min(index + batchSize, pendingRequests.count)
                        let batch = pendingRequests[index..<end]
                        index = end

                        await withTaskGroup(of: Void.self) { group in
                            for (trackId, path) in batch {
                                group.addTask {
                                    let _ = await MetadataService.loadMetadata(
                                        for: trackId,
                                        path: path,
                                        engine: engine,
                                        apiKey: apiKey
                                    )
                                }
                            }
                        }
                    }
                }
                
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
                if engine.currentTrackId == 0 {
                    let dbPath = Self.persistentDatabasePath()

                    // 1. 优先使用 CLI 保存的共享播放列表（与 automix-playlist 一致）
                    if let ids = Self.loadSharedPlaylist(dbPath: dbPath) {
                        let playlist = try engine.createPlaylist(trackIds: ids)
                        requestedPlaylistCount = ids.count
                        playlistTrackIds = ids
                        try engine.play(playlist: playlist)
                        refreshCurrentTrackInfo(trackId: engine.currentTrackId)
                        return
                    }

                    // 2. 无共享列表时，生成新列表并保存
                    let allTracks = try engine.searchTracks(pattern: "%")
                    if !allTracks.isEmpty {
                        let playlistCount = min(10, allTracks.count)
                        let playlist: AutoMixPlaylist
                        if mixEnabled {
                            let seed = allTracks.first!
                            playlist = try engine.generatePlaylist(seedTrackId: seed, count: playlistCount)
                        } else {
                            let shuffled = allTracks.shuffled()
                            let ids = Array(shuffled.prefix(playlistCount))
                            playlist = try engine.createPlaylist(trackIds: ids)
                        }
                        requestedPlaylistCount = playlistCount
                        do {
                            playlistTrackIds = try playlist.getTrackIDs()
                            Self.saveSharedPlaylist(dbPath: dbPath, trackIds: playlistTrackIds)
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
        guard let engine = engine, !isTransitioning else { return }
        do {
            try engine.next()
        } catch {
            statusMessage = "Next failed: \(error)"
        }
    }
    
    func seek(to seconds: Float) {
        guard let engine = engine, !isTransitioning else { return }
        do {
            try engine.seek(seconds: seconds)
        } catch {
            statusMessage = "Seek failed: \(error)"
        }
    }
    
    /// 在 Demo 中创建播放列表并播放（与 CLI 的 --seed/--count 一致，会保存到共享文件）
    func createAndPlayPlaylist(seedTrackId: Int64, count: Int) {
        guard let engine = engine else { return }
        do {
            // 若正在播放或暂停，先 stop，以便能创建新列表（Demo 无单独 Stop 按钮）
            if engine.currentTrackId != 0 {
                try engine.stop()
                currentTrackId = 0
                currentTrackInfo = nil
                currentTrackMetadata = nil
                nextTrackInfo = nil
                nextMetadataTask?.cancel()
                nextMetadataTask = nil
                playlistTrackIds = []
                position = 0
            }
            let playlist: AutoMixPlaylist
            var preCreateIds: [Int64] = []
            if mixEnabled {
                playlist = try engine.generatePlaylist(seedTrackId: seedTrackId, count: count)
            } else {
                let allTracks = try engine.searchTracks(pattern: "%")
                preCreateIds = Self._shuffledWithSeedFirst(allTracks: allTracks, seedTrackId: seedTrackId, count: count)
                playlist = try engine.createPlaylist(trackIds: preCreateIds)
            }
            let ids = (try? playlist.getTrackIDs()) ?? preCreateIds
            requestedPlaylistCount = count
            playlistTrackIds = ids
            Self.saveSharedPlaylist(dbPath: Self.persistentDatabasePath(), trackIds: ids)
            try engine.play(playlist: playlist)
            refreshCurrentTrackInfo(trackId: engine.currentTrackId)
        } catch {
            statusMessage = "Create playlist failed: \(error)"
        }
    }

    func previous() {
        guard let engine = engine, !isTransitioning else { return }
        do {
            try engine.previous()
        } catch {
            statusMessage = "Previous failed: \(error)"
        }
    }
}
