import Foundation
import UserNotifications

private func systemNotifierLog(_ message: String) {
    print("[SystemNotifier] \(message)")
}

@MainActor
final class SystemNotifier {
    static let shared = SystemNotifier()

    private lazy var center: UNUserNotificationCenter? = {
        let bundleId = Bundle.main.bundleIdentifier ?? "(nil)"
        systemNotifierLog("init center, bundleId=\(bundleId), executable=\(Bundle.main.executablePath ?? "(nil)")")
        // `swift run` 启动的可执行程序在某些环境下初始化通知中心会崩溃；
        // 无 bundle id 时降级为 no-op，避免影响主流程。
        guard let bundleId = Bundle.main.bundleIdentifier, !bundleId.isEmpty else {
            systemNotifierLog("center disabled: missing bundle id (possible swift run environment)")
            return nil
        }
        systemNotifierLog("center ready for bundleId=\(bundleId)")
        return UNUserNotificationCenter.current()
    }()
    private var lastFailureAt: Date?
    private var suppressedFailureCount: Int = 0
    private let failureThrottleSeconds: TimeInterval = 15

    private init() {}

    func requestAuthorizationIfNeeded() {
        guard let center else {
            systemNotifierLog("skip requestAuthorization: center unavailable")
            return
        }
        systemNotifierLog("requestAuthorization start")
        center.requestAuthorization(options: [.alert, .sound]) { granted, error in
            if let error {
                systemNotifierLog("requestAuthorization failed: \(error.localizedDescription)")
                return
            }
            systemNotifierLog("requestAuthorization result: granted=\(granted)")
        }
    }

    func notifyScrobbleSuccess(artist: String, track: String, retried: Bool) {
        let title = retried ? "Last.fm 重试成功" : "Last.fm 记录成功"
        post(title: title, body: "\(artist) - \(track)")
    }

    func notifyScrobbleFailure(error: String, queued: Bool) {
        let now = Date()
        if let lastFailureAt, now.timeIntervalSince(lastFailureAt) < failureThrottleSeconds {
            suppressedFailureCount += 1
            systemNotifierLog("failure notification throttled, suppressed=\(suppressedFailureCount)")
            return
        }
        lastFailureAt = now
        let extra = suppressedFailureCount > 0 ? "（另有 \(suppressedFailureCount) 条失败已合并）" : ""
        suppressedFailureCount = 0
        let title = queued ? "Last.fm 记录失败，已加入重试队列" : "Last.fm 记录失败"
        post(title: title, body: "\(error)\(extra)")
    }

    private func post(title: String, body: String) {
        guard let center else {
            systemNotifierLog("drop notification: center unavailable, title=\(title)")
            return
        }
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        content.sound = .default

        let requestId = "automix.lastfm.\(UUID().uuidString)"
        let request = UNNotificationRequest(
            identifier: requestId,
            content: content,
            trigger: nil
        )
        systemNotifierLog("enqueue notification: title=\(title), body=\(body)")
        center.add(request) { error in
            if let error {
                systemNotifierLog("notification enqueue failed: \(error.localizedDescription)")
            } else {
                systemNotifierLog("notification enqueue success: id=\(requestId)")
            }
        }
    }
}
