//
//  MetadataService.swift
//  AutomixDemo
//

import Foundation
import AppKit
import Automix

enum MetadataService {
    private static let inflightTasks = InflightTasks()
    
    static func loadMetadata(
        for trackId: Int64,
        path: String,
        engine: AutoMixEngine,
        apiKey: String?
    ) async -> TrackMetadata {
        let task = await inflightTasks.taskFor(trackId: trackId) {
            await loadMetadataImpl(for: trackId, path: path, engine: engine, apiKey: apiKey)
        }
        let result = await task.value
        await inflightTasks.cleanup(trackId: trackId)
        return result
    }
    
    private static func loadMetadataImpl(
        for trackId: Int64,
        path: String,
        engine: AutoMixEngine,
        apiKey: String?
    ) async -> TrackMetadata {
        let fileName = (path as NSString).lastPathComponent
        print("[Metadata] [\(trackId)] START \(fileName) apiKey=\(apiKey != nil ? "yes" : "nil")")
        
        // 1. Try file metadata
        let fileMetadata = await TrackMetadataLoader.loadMetadata(from: path)
        let hasCompleteFileMetadata = (fileMetadata.title != nil && !fileMetadata.title!.isEmpty) &&
                                      (fileMetadata.artist != nil && !fileMetadata.artist!.isEmpty)
        print("[Metadata] [\(trackId)] file tags: title=\(fileMetadata.title ?? "(nil)") artist=\(fileMetadata.artist ?? "(nil)") artwork=\(fileMetadata.artwork != nil ? "yes" : "nil") complete=\(hasCompleteFileMetadata)")
        
        if hasCompleteFileMetadata && fileMetadata.artwork != nil {
            await saveToDB(engine: engine, trackId: trackId, metadata: fileMetadata, source: "file")
            print("[Metadata] [\(trackId)] DONE via file tags (with artwork)")
            return fileMetadata
        }
        
        // 2. Check DB — only use cached data if it has meaningful content or all sources were tried
        let dbMetadata: AutoMixTrackMetadata? = await MainActor.run {
            try? engine.getTrackMetadata(trackId: trackId)
        }
        if let dbMetadata, dbMetadata.fetchedAt > 0 {
            let hasContent = !(dbMetadata.title ?? "").isEmpty || !(dbMetadata.artist ?? "").isEmpty
            let hasArtwork = dbMetadata.artworkData != nil && !dbMetadata.artworkData!.isEmpty
            let fullyAttempted = dbMetadata.source == "acoustid" || dbMetadata.source == "filename" || dbMetadata.source == "none"
            print("[Metadata] [\(trackId)] DB cache: source=\(dbMetadata.source ?? "(nil)") title=\(dbMetadata.title ?? "(nil)") hasContent=\(hasContent) hasArtwork=\(hasArtwork) fullyAttempted=\(fullyAttempted)")
            if (hasContent && hasArtwork) || fullyAttempted {
                let artwork = dbMetadata.artworkData.flatMap { NSImage(data: $0) } ?? fileMetadata.artwork
                print("[Metadata] [\(trackId)] DONE via DB cache")
                return TrackMetadata(title: dbMetadata.title, artist: dbMetadata.artist, album: dbMetadata.album, artwork: artwork)
            }
            print("[Metadata] [\(trackId)] DB cache skipped (missing artwork or incomplete)")
        } else {
            print("[Metadata] [\(trackId)] DB cache: miss")
        }
        
        // 3. If file tags have title+artist but missing artwork, skip AcoustID and search cover directly
        if hasCompleteFileMetadata {
            print("[Metadata] [\(trackId)] file has title+artist but no artwork, searching cover art...")
            var coverArt: NSImage?
            if let artist = fileMetadata.artist, !artist.isEmpty {
                coverArt = await searchCoverArt(artist: artist, album: fileMetadata.album, trackId: trackId)
            }
            let result = TrackMetadata(title: fileMetadata.title, artist: fileMetadata.artist, album: fileMetadata.album, artwork: coverArt)
            await saveToDB(engine: engine, trackId: trackId, metadata: result, source: "file")
            print("[Metadata] [\(trackId)] DONE via file tags + cover search (artwork=\(coverArt != nil ? "ok" : "nil"))")
            return result
        }
        
        // 4. Fallback to AcoustID if API key available and fpcalc works
        guard let apiKey = apiKey, !apiKey.isEmpty else {
            print("[Metadata] [\(trackId)] DONE no apiKey, returning file metadata")
            return fileMetadata
        }
        
        print("[Metadata] [\(trackId)] running fpcalc...")
        guard let (fpDuration, fingerprint) = await runFpcalc(path: path) else {
            print("[Metadata] [\(trackId)] DONE fpcalc failed")
            return fileMetadata
        }
        print("[Metadata] [\(trackId)] fpcalc ok: duration=\(fpDuration)s fingerprint=\(fingerprint.prefix(20))...")
        
        print("[Metadata] [\(trackId)] querying AcoustID...")
        if let acoustidMd = await lookupAcoustID(fingerprint: fingerprint, duration: fpDuration, apiKey: apiKey) {
            print("[Metadata] [\(trackId)] AcoustID hit: title=\(acoustidMd.title ?? "(nil)") artist=\(acoustidMd.artist ?? "(nil)") album=\(acoustidMd.album ?? "(nil)") mbid=\(acoustidMd.releaseGroupMBID ?? "(nil)")")
            let title = fileMetadata.title ?? acoustidMd.title
            let artist = fileMetadata.artist ?? acoustidMd.artist
            let album = fileMetadata.album ?? acoustidMd.album
            
            var coverArt: NSImage? = fileMetadata.artwork
            if coverArt == nil, let mbid = acoustidMd.releaseGroupMBID {
                print("[Metadata] [\(trackId)] fetching cover art for mbid=\(mbid)...")
                coverArt = await fetchCoverArt(releaseGroupMBID: mbid)
                print("[Metadata] [\(trackId)] cover art via CoverArt Archive: \(coverArt != nil ? "ok" : "not found")")
            }
            if coverArt == nil, let artist = artist, !artist.isEmpty {
                print("[Metadata] [\(trackId)] trying MusicBrainz/iTunes for cover art...")
                coverArt = await searchCoverArt(artist: artist, album: album, trackId: trackId)
            }
            
            let merged = TrackMetadata(title: title, artist: artist, album: album, artwork: coverArt)
            await saveToDB(engine: engine, trackId: trackId, metadata: merged, source: "acoustid")
            print("[Metadata] [\(trackId)] DONE via AcoustID")
            return merged
        } else {
            print("[Metadata] [\(trackId)] AcoustID returned no results, trying filename fallback...")
        }
        
        // 4. Filename parsing fallback
        let parsed = parseFilename(path: path)
        print("[Metadata] [\(trackId)] filename parsed: title=\(parsed.title ?? "(nil)") artist=\(parsed.artist ?? "(nil)") album=\(parsed.album ?? "(nil)")")
        
        let fnTitle = fileMetadata.title ?? parsed.title
        let fnArtist = fileMetadata.artist ?? parsed.artist
        let fnAlbum = fileMetadata.album ?? parsed.album
        
        var coverArt: NSImage? = fileMetadata.artwork
        if coverArt == nil, let artist = fnArtist, !artist.isEmpty {
            coverArt = await searchCoverArt(artist: artist, album: fnAlbum, trackId: trackId)
        }
        
        let result = TrackMetadata(title: fnTitle, artist: fnArtist, album: fnAlbum, artwork: coverArt)
        let source = (fnTitle != nil || fnArtist != nil) ? "filename" : "none"
        await saveToDB(engine: engine, trackId: trackId, metadata: result, source: source)
        print("[Metadata] [\(trackId)] DONE via \(source)")
        return result
    }
    
    private static func saveToDB(engine: AutoMixEngine, trackId: Int64, metadata: TrackMetadata, source: String) async {
        let artworkData = metadata.artwork?.tiffRepresentation
        let amMetadata = AutoMixTrackMetadata(
            title: metadata.title,
            artist: metadata.artist,
            album: metadata.album,
            artworkData: artworkData,
            source: source,
            fetchedAt: Int64(Date().timeIntervalSince1970)
        )
        await MainActor.run {
            try? engine.setTrackMetadata(trackId: trackId, metadata: amMetadata)
        }
    }
    
    private static func runFpcalc(path: String) async -> (Int, String)? {
        let task = Process()
        
        let possiblePaths = [
            "/opt/homebrew/bin/fpcalc",
            "/usr/local/bin/fpcalc",
            "/usr/bin/fpcalc",
            Bundle.main.path(forResource: "fpcalc", ofType: nil)
        ].compactMap { $0 }
        
        guard let execPath = possiblePaths.first(where: { FileManager.default.fileExists(atPath: $0) }) else {
            print("[fpcalc] not found in any known path")
            return nil
        }
        
        task.executableURL = URL(fileURLWithPath: execPath)
        task.arguments = ["-json", "-length", "120", path]
        print("[fpcalc] exec: \(execPath) -json -length 120 \((path as NSString).lastPathComponent)")
        
        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        task.standardOutput = stdoutPipe
        task.standardError = stderrPipe
        
        do {
            try task.run()
            let stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
            let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
            task.waitUntilExit()
            
            let exitCode = task.terminationStatus
            let stderrStr = String(data: stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
            
            if exitCode != 0 {
                print("[fpcalc] exit code \(exitCode), stderr: \(stderrStr)")
                return nil
            }
            
            if stdoutData.isEmpty {
                print("[fpcalc] exit 0 but stdout is empty, stderr: \(stderrStr)")
                return nil
            }
            
            guard let json = try JSONSerialization.jsonObject(with: stdoutData) as? [String: Any] else {
                let raw = String(data: stdoutData.prefix(200), encoding: .utf8) ?? "(binary)"
                print("[fpcalc] output is not a JSON object: \(raw)")
                return nil
            }
            
            guard let duration = json["duration"] as? Int ?? (json["duration"] as? Double).map({ Int($0) }) else {
                print("[fpcalc] missing or invalid 'duration' in JSON: \(json.keys.sorted())")
                return nil
            }
            guard let fingerprint = json["fingerprint"] as? String else {
                print("[fpcalc] missing 'fingerprint' in JSON: \(json.keys.sorted())")
                return nil
            }
            
            return (duration, fingerprint)
        } catch {
            print("[fpcalc] launch failed: \(error)")
        }
        return nil
    }
    
    private struct AcoustIDResult {
        var title: String?
        var artist: String?
        var album: String?
        var releaseGroupMBID: String?
    }
    
    // Limits requests to <= 2.5 times per second
    private static let rateLimiter = RateLimiter(interval: 0.4)
    
    private static func lookupAcoustID(fingerprint: String, duration: Int, apiKey: String) async -> AcoustIDResult? {
        await rateLimiter.wait()
        
        var components = URLComponents(string: "https://api.acoustid.org/v2/lookup")!
        components.queryItems = [
            URLQueryItem(name: "client", value: apiKey),
            URLQueryItem(name: "duration", value: String(duration)),
            URLQueryItem(name: "fingerprint", value: fingerprint),
            URLQueryItem(name: "meta", value: "recordings+releasegroups+compress")
        ]
        
        guard let url = components.url else { return nil }
        
        do {
            var request = URLRequest(url: url)
            request.httpMethod = "POST"
            let (data, _) = try await shieldedData(for: request)
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                print("[AcoustID] response is not valid JSON")
                return nil
            }
            let status = json["status"] as? String
            guard status == "ok" else {
                print("[AcoustID] status=\(status ?? "(nil)") error=\(json["error"] ?? "(none)")")
                return nil
            }
            guard let results = json["results"] as? [[String: Any]], !results.isEmpty else {
                print("[AcoustID] no results in response")
                return nil
            }
            
            let bestResult = results.sorted { ($0["score"] as? Double ?? 0) > ($1["score"] as? Double ?? 0) }.first!
            let score = bestResult["score"] as? Double ?? 0
            guard let recordings = bestResult["recordings"] as? [[String: Any]], let bestRecording = recordings.first else {
                print("[AcoustID] best result (score=\(score)) has no recordings")
                return nil
            }
            
            var res = AcoustIDResult()
            res.title = bestRecording["title"] as? String
            
            if let artists = bestRecording["artists"] as? [[String: Any]] {
                res.artist = artists.compactMap { $0["name"] as? String }.joined(separator: ", ")
            }
            
            if let releasegroups = bestRecording["releasegroups"] as? [[String: Any]], let bestRG = releasegroups.first {
                res.album = bestRG["title"] as? String
                res.releaseGroupMBID = bestRG["id"] as? String
            }
            
            return res
            
        } catch {
            print("AcoustID lookup failed: \(error)")
            return nil
        }
    }
    
    // MARK: - Filename Parsing
    
    private static func parseFilename(path: String) -> (title: String?, artist: String?, album: String?) {
        let filename = ((path as NSString).lastPathComponent as NSString).deletingPathExtension
        
        // Pattern 1: "Artist [Year] Album [Track#] Title"
        if let match = try? NSRegularExpression(pattern: #"^(.+?)\s*\[\d{4}\]\s*(.+?)\s*\[\d+\]\s*(.+)$"#)
            .firstMatch(in: filename, range: NSRange(filename.startIndex..., in: filename)),
           match.numberOfRanges == 4 {
            let artist = String(filename[Range(match.range(at: 1), in: filename)!]).trimmingCharacters(in: .whitespaces)
            let album = String(filename[Range(match.range(at: 2), in: filename)!]).trimmingCharacters(in: .whitespaces)
            let title = String(filename[Range(match.range(at: 3), in: filename)!]).trimmingCharacters(in: .whitespaces)
            return (title, artist, album)
        }
        
        let parts = filename.components(separatedBy: " - ")
        
        // Pattern 2: "Artist - Album - Track# Title" or "Artist - Album - Title"
        if parts.count >= 3 {
            let artist = parts[0].trimmingCharacters(in: .whitespaces)
            let album = parts[1].trimmingCharacters(in: .whitespaces)
            let raw = parts[2...].joined(separator: " - ").trimmingCharacters(in: .whitespaces)
            let title = raw.replacingOccurrences(of: #"^\d+[\s\.\-]*"#, with: "", options: .regularExpression)
                .trimmingCharacters(in: .whitespaces)
            return (title.isEmpty ? raw : title, artist, album)
        }
        
        // Pattern 3: "Artist - Title"
        if parts.count == 2 {
            let artist = parts[0].trimmingCharacters(in: .whitespaces)
            let raw = parts[1].trimmingCharacters(in: .whitespaces)
            let title = raw.replacingOccurrences(of: #"^\d+[\s\.\-]*"#, with: "", options: .regularExpression)
                .trimmingCharacters(in: .whitespaces)
            return (title.isEmpty ? raw : title, artist, nil)
        }
        
        // Pattern 4: "Track# Title" — strip leading track number
        let stripped = filename.replacingOccurrences(of: #"^\d+[\s\.\-]+"#, with: "", options: .regularExpression)
        let title = stripped.trimmingCharacters(in: .whitespaces)
        return (title.isEmpty ? nil : title, nil, nil)
    }
    
    // MARK: - Cover Art Search (MusicBrainz → iTunes fallback)
    
    private static let mbRateLimiter = RateLimiter(interval: 1.1)
    
    private static func searchCoverArt(artist: String, album: String?, trackId: Int64) async -> NSImage? {
        // Try MusicBrainz → Cover Art Archive
        if let album = album, !album.isEmpty {
            if let mbid = await searchMusicBrainz(artist: artist, album: album) {
                print("[Metadata] [\(trackId)] MusicBrainz found mbid=\(mbid)")
                if let cover = await fetchCoverArt(releaseGroupMBID: mbid) {
                    print("[Metadata] [\(trackId)] cover art via MusicBrainz: ok")
                    return cover
                }
                print("[Metadata] [\(trackId)] Cover Art Archive has no image for this mbid")
            } else {
                print("[Metadata] [\(trackId)] MusicBrainz: no match for artist=\(artist) album=\(album)")
            }
        }
        
        // Fallback: iTunes Search
        let searchTerm = [artist, album].compactMap { $0 }.joined(separator: " ")
        if let cover = await fetchiTunesCoverArt(query: searchTerm) {
            print("[Metadata] [\(trackId)] cover art via iTunes: ok")
            return cover
        }
        print("[Metadata] [\(trackId)] iTunes: no cover art found")
        return nil
    }
    
    private static func escapeLuceneQueryTerm(_ term: String) -> String {
        var escaped = ""
        for ch in term {
            switch ch {
            case "+", "-", "&", "|", "!", "(", ")", "{", "}", "[", "]", "^", "\"", "~", "*", "?", ":", "\\", "/":
                escaped.append("\\")
                escaped.append(ch)
            default:
                escaped.append(ch)
            }
        }
        return escaped
    }

    private static func searchMusicBrainz(artist: String, album: String) async -> String? {
        await mbRateLimiter.wait()
        
        let escapedArtist = escapeLuceneQueryTerm(artist)
        let escapedAlbum = escapeLuceneQueryTerm(album)
        let query = "artist:\"\(escapedArtist)\" AND releasegroup:\"\(escapedAlbum)\""
        var components = URLComponents(string: "https://musicbrainz.org/ws/2/release-group/")!
        components.queryItems = [
            URLQueryItem(name: "query", value: query),
            URLQueryItem(name: "fmt", value: "json"),
            URLQueryItem(name: "limit", value: "1")
        ]
        guard let url = components.url else { return nil }
        
        var request = URLRequest(url: url)
        request.setValue("AutoMixDemo/1.0.0 ( automix-demo@example.com )", forHTTPHeaderField: "User-Agent")
        
        do {
            let (data, _) = try await shieldedData(for: request)
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let releaseGroups = json["release-groups"] as? [[String: Any]],
                  let first = releaseGroups.first,
                  let mbid = first["id"] as? String else {
                return nil
            }
            let score = first["score"] as? Int ?? 0
            print("[MusicBrainz] match: \(first["title"] ?? "") score=\(score) mbid=\(mbid)")
            guard score >= 80 else {
                print("[MusicBrainz] score too low (\(score)), skipping")
                return nil
            }
            return mbid
        } catch {
            print("[MusicBrainz] search failed: \(error)")
            return nil
        }
    }
    
    private static func fetchiTunesCoverArt(query: String) async -> NSImage? {
        var components = URLComponents(string: "https://itunes.apple.com/search")!
        components.queryItems = [
            URLQueryItem(name: "term", value: query),
            URLQueryItem(name: "media", value: "music"),
            URLQueryItem(name: "entity", value: "album"),
            URLQueryItem(name: "limit", value: "1")
        ]
        guard let url = components.url else { return nil }
        
        do {
            let (data, _) = try await shieldedData(for: URLRequest(url: url))
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let results = json["results"] as? [[String: Any]],
                  let first = results.first,
                  let artworkUrl = first["artworkUrl100"] as? String else {
                return nil
            }
            let hiRes = artworkUrl.replacingOccurrences(of: "100x100", with: "600x600")
            print("[iTunes] found: \(first["collectionName"] ?? "") artwork=\(hiRes)")
            guard let imageUrl = URL(string: hiRes) else { return nil }
            let (imageData, _) = try await shieldedData(from: imageUrl)
            return NSImage(data: imageData)
        } catch {
            print("[iTunes] search failed: \(error)")
            return nil
        }
    }
    
    private static func fetchCoverArt(releaseGroupMBID: String) async -> NSImage? {
        guard let url = URL(string: "https://coverartarchive.org/release-group/\(releaseGroupMBID)/front-500") else { return nil }
        
        var request = URLRequest(url: url)
        request.setValue("AutoMixDemo/1.0.0 ( automix-demo@example.com )", forHTTPHeaderField: "User-Agent")
        
        do {
            let (data, response) = try await shieldedData(for: request)
            guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
                return nil
            }
            return NSImage(data: data)
        } catch {
            print("Cover Art fetch failed: \(error)")
            return nil
        }
    }
    
    // MARK: - Cancellation-shielded networking
    
    private static func shieldedData(for request: URLRequest) async throws -> (Data, URLResponse) {
        try await withCheckedThrowingContinuation { continuation in
            URLSession.shared.dataTask(with: request) { data, response, error in
                if let error {
                    continuation.resume(throwing: error)
                } else if let data, let response {
                    continuation.resume(returning: (data, response))
                } else {
                    continuation.resume(throwing: URLError(.unknown))
                }
            }.resume()
        }
    }
    
    private static func shieldedData(from url: URL) async throws -> (Data, URLResponse) {
        try await shieldedData(for: URLRequest(url: url))
    }
}

private actor InflightTasks {
    private var tasks: [Int64: Task<TrackMetadata, Never>] = [:]
    
    func taskFor(trackId: Int64, create: @Sendable @escaping () async -> TrackMetadata) -> Task<TrackMetadata, Never> {
        if let existing = tasks[trackId] {
            return existing
        }
        let task = Task { await create() }
        tasks[trackId] = task
        return task
    }
    
    func cleanup(trackId: Int64) {
        tasks.removeValue(forKey: trackId)
    }
}

actor RateLimiter {
    private let interval: TimeInterval
    private var lastRequestTime: Date = .distantPast

    init(interval: TimeInterval) {
        self.interval = interval
    }

    func wait() async {
        let now = Date()
        let timeSinceLast = now.timeIntervalSince(lastRequestTime)
        if timeSinceLast < interval {
            let delay = interval - timeSinceLast
            try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
        }
        lastRequestTime = Date()
    }
}
