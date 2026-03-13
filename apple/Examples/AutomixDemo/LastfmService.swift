import Foundation
import CryptoKit

enum LastfmService {
    struct Session {
        let name: String
        let key: String
    }

    struct ScrobblePayload {
        let artist: String
        let track: String
        let album: String?
        let timestamp: Int64
    }

    enum LastfmError: LocalizedError {
        case invalidResponse
        case httpStatus(code: Int, body: String)
        case apiError(code: Int, message: String)
        case missingToken
        case missingSessionKey
        case scrobbleIgnored(code: Int, message: String)

        var errorDescription: String? {
            switch self {
            case .invalidResponse:
                return "Invalid Last.fm response."
            case .httpStatus(let code, _):
                return "Last.fm HTTP error \(code)."
            case .apiError(let code, let message):
                return "Last.fm error \(code): \(message)"
            case .missingToken:
                return "Missing Last.fm auth token."
            case .missingSessionKey:
                return "Missing Last.fm session key."
            case .scrobbleIgnored(let code, let message):
                return "Last.fm scrobble ignored (code \(code)): \(message)"
            }
        }
    }

    static func isRetryableScrobbleError(_ error: Error) -> Bool {
        if let urlError = error as? URLError {
            log("retryable=true (URLError code=\(urlError.code.rawValue))")
            return true
        }
        guard let lastfmError = error as? LastfmError else {
            log("retryable=true (unknown error type: \(type(of: error)))")
            return true
        }

        switch lastfmError {
        case .scrobbleIgnored:
            log("retryable=false (scrobble ignored)")
            return false
        case .missingToken, .missingSessionKey:
            log("retryable=false (auth/session missing)")
            return false
        case .apiError(let code, _):
            let retryable = (code == 11 || code == 16)
            log("retryable=\(retryable) (apiError code=\(code))")
            return retryable
        case .httpStatus(let code, _):
            let retryable = (code == 408 || code == 429 || code >= 500)
            log("retryable=\(retryable) (httpStatus code=\(code))")
            return retryable
        case .invalidResponse:
            log("retryable=true (invalid response)")
            return true
        }
    }

    static func authorizationURL(apiKey: String, token: String) -> URL? {
        var components = URLComponents(string: "https://www.last.fm/api/auth/")!
        components.queryItems = [
            URLQueryItem(name: "api_key", value: apiKey),
            URLQueryItem(name: "token", value: token)
        ]
        return components.url
    }

    static func fetchAuthToken(apiKey: String) async throws -> String {
        var params: [String: String] = [
            "method": "auth.getToken",
            "api_key": apiKey
        ]
        params["format"] = "json"
        let json = try await request(params: params)
        guard let token = json["token"] as? String, !token.isEmpty else {
            throw LastfmError.invalidResponse
        }
        return token
    }

    static func fetchSession(apiKey: String, sharedSecret: String, token: String) async throws -> Session {
        guard !token.isEmpty else { throw LastfmError.missingToken }
        var params: [String: String] = [
            "method": "auth.getSession",
            "api_key": apiKey,
            "token": token
        ]
        params["api_sig"] = sign(params: params, sharedSecret: sharedSecret)
        params["format"] = "json"

        let json = try await request(params: params)
        guard let session = json["session"] as? [String: Any] else {
            throw LastfmError.invalidResponse
        }
        guard
            let name = session["name"] as? String,
            let key = session["key"] as? String,
            !key.isEmpty
        else {
            throw LastfmError.invalidResponse
        }
        return Session(name: name, key: key)
    }

    static func scrobble(
        apiKey: String,
        sharedSecret: String,
        sessionKey: String,
        payload: ScrobblePayload
    ) async throws {
        guard !sessionKey.isEmpty else { throw LastfmError.missingSessionKey }
        var params: [String: String] = [
            "method": "track.scrobble",
            "api_key": apiKey,
            "sk": sessionKey,
            "artist": payload.artist,
            "track": payload.track,
            "timestamp": String(payload.timestamp)
        ]
        if let album = payload.album, !album.isEmpty {
            params["album"] = album
        }
        params["api_sig"] = sign(params: params, sharedSecret: sharedSecret)
        params["format"] = "json"
        let json = try await request(params: params)
        try validateScrobbleResult(json)
    }

    private static func request(params: [String: String]) async throws -> [String: Any] {
        guard let url = URL(string: "https://ws.audioscrobbler.com/2.0/") else {
            throw LastfmError.invalidResponse
        }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded; charset=utf-8", forHTTPHeaderField: "Content-Type")
        let bodyString = formURLEncodedBody(params: params)
        request.httpBody = bodyString.data(using: .utf8)
        log("-> POST \(url.absoluteString)")
        log("-> body: \(redactedBodyLogString(from: params))")

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await shieldedData(for: request)
        } catch {
            log("<- transport error: \(error.localizedDescription)")
            throw error
        }
        let responseText = String(data: data, encoding: .utf8) ?? "<non-utf8 \(data.count) bytes>"
        if let http = response as? HTTPURLResponse {
            log("<- status: \(http.statusCode)")
        } else {
            log("<- non-http response: \(type(of: response))")
        }
        log("<- body: \(responseText)")
        guard let http = response as? HTTPURLResponse else {
            throw LastfmError.invalidResponse
        }
        guard (200...299).contains(http.statusCode) else {
            log("<- http error status=\(http.statusCode)")
            throw LastfmError.httpStatus(code: http.statusCode, body: responseText)
        }

        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            log("<- json parse failed")
            throw LastfmError.invalidResponse
        }
        if let errorCode = json["error"] as? Int {
            let message = (json["message"] as? String) ?? "Unknown error"
            log("<- api error \(errorCode): \(message)")
            throw LastfmError.apiError(code: errorCode, message: message)
        }
        log("<- api success")
        return json
    }

    private static func validateScrobbleResult(_ json: [String: Any]) throws {
        guard let scrobbles = json["scrobbles"] as? [String: Any],
              let attr = scrobbles["@attr"] as? [String: Any]
        else {
            log("<- scrobble response missing @attr, fallback to legacy success")
            return
        }

        let accepted = parseInt(attr["accepted"]) ?? 0
        let ignored = parseInt(attr["ignored"]) ?? 0

        let (ignoredCode, ignoredMessage) = parseIgnoredMessage(from: scrobbles["scrobble"])
        log("<- scrobble parsed accepted=\(accepted), ignored=\(ignored), ignoredCode=\(ignoredCode), ignoredMessage=\(ignoredMessage)")

        if accepted > 0 && ignored == 0 && ignoredCode == 0 {
            return
        }
        throw LastfmError.scrobbleIgnored(code: ignoredCode, message: ignoredMessage)
    }

    private static func parseIgnoredMessage(from scrobbleNode: Any?) -> (code: Int, message: String) {
        if let scrobble = scrobbleNode as? [String: Any] {
            return parseIgnoredMessage(fromScrobbleDict: scrobble)
        }
        if let scrobbles = scrobbleNode as? [[String: Any]] {
            for entry in scrobbles {
                let parsed = parseIgnoredMessage(fromScrobbleDict: entry)
                if parsed.code != 0 {
                    return parsed
                }
            }
        }
        return (0, "")
    }

    private static func parseIgnoredMessage(fromScrobbleDict scrobble: [String: Any]) -> (code: Int, message: String) {
        guard let ignoredMessage = scrobble["ignoredMessage"] as? [String: Any] else {
            return (0, "")
        }
        let code = parseInt(ignoredMessage["code"]) ?? 0
        let message = (ignoredMessage["#text"] as? String) ?? ""
        return (code, message)
    }

    private static func parseInt(_ value: Any?) -> Int? {
        if let intValue = value as? Int {
            return intValue
        }
        if let stringValue = value as? String {
            return Int(stringValue)
        }
        if let numberValue = value as? NSNumber {
            return numberValue.intValue
        }
        return nil
    }

    private static func sign(params: [String: String], sharedSecret: String) -> String {
        let sorted = params
            .filter { $0.key != "format" && $0.key != "callback" }
            .sorted { $0.key < $1.key }
        let base = sorted.map { "\($0.key)\($0.value)" }.joined() + sharedSecret
        let digest = Insecure.MD5.hash(data: Data(base.utf8))
        return digest.map { String(format: "%02x", $0) }.joined()
    }

    private static func formURLEncodedBody(params: [String: String]) -> String {
        params
            .map { key, value in
                "\(urlEncode(key))=\(urlEncode(value))"
            }
            .sorted()
            .joined(separator: "&")
    }

    private static func urlEncode(_ value: String) -> String {
        let allowed = CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }

    private static func redactedBodyLogString(from params: [String: String]) -> String {
        var output = params
        if let sk = output["sk"] {
            output["sk"] = maskSecret(sk)
        }
        if let apiSig = output["api_sig"] {
            output["api_sig"] = maskSecret(apiSig)
        }
        return output
            .map { key, value in
                "\(urlEncode(key))=\(urlEncode(value))"
            }
            .sorted()
            .joined(separator: "&")
    }

    private static func maskSecret(_ value: String) -> String {
        guard value.count > 8 else { return "***" }
        let prefix = value.prefix(4)
        let suffix = value.suffix(4)
        return "\(prefix)...\(suffix)"
    }

    private static func log(_ message: String) {
        print("[LastfmService] \(message)")
    }

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
}
