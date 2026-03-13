import Foundation

struct PendingScrobbleEvent: Codable, Equatable {
    let eventId: String
    let trackId: Int64
    let artist: String
    let track: String
    let album: String?
    let timestamp: Int64
    let createdAt: Int64
    var retries: Int
    var lastError: String?
}

actor ScrobbleQueueStore {
    private let fileURL: URL

    init(fileURL: URL = ScrobbleQueueStore.defaultFileURL()) {
        self.fileURL = fileURL
    }

    func all() -> [PendingScrobbleEvent] {
        load()
    }

    func enqueueIfMissing(_ event: PendingScrobbleEvent) {
        var events = load()
        guard !events.contains(where: { $0.eventId == event.eventId }) else {
            return
        }
        events.append(event)
        events.sort { $0.createdAt < $1.createdAt }
        save(events)
    }

    func remove(eventId: String) {
        var events = load()
        events.removeAll { $0.eventId == eventId }
        save(events)
    }

    func markFailure(eventId: String, error: String) {
        var events = load()
        guard let idx = events.firstIndex(where: { $0.eventId == eventId }) else { return }
        events[idx].retries += 1
        events[idx].lastError = error
        save(events)
    }

    static func defaultFileURL() -> URL {
        let fileManager = FileManager.default
        if let appSupport = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            let automixDir = appSupport.appendingPathComponent("AutomixDemo", isDirectory: true)
            try? fileManager.createDirectory(at: automixDir, withIntermediateDirectories: true)
            return automixDir.appendingPathComponent("lastfm_scrobble_queue.json")
        }
        return fileManager.temporaryDirectory.appendingPathComponent("lastfm_scrobble_queue.json")
    }

    private func load() -> [PendingScrobbleEvent] {
        guard let data = try? Data(contentsOf: fileURL) else { return [] }
        let decoder = JSONDecoder()
        return (try? decoder.decode([PendingScrobbleEvent].self, from: data)) ?? []
    }

    private func save(_ events: [PendingScrobbleEvent]) {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(events) else { return }
        try? data.write(to: fileURL, options: .atomic)
    }
}
