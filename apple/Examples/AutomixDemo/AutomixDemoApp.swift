import SwiftUI
import Automix
import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // 确保通过 swift run 运行时也能作为普通应用激活
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        guard let w = mainContentWindow else { return }
        w.level = .floating
        w.makeKeyAndOrderFront(nil)
    }

    func applicationDidResignActive(_ notification: Notification) {
        mainContentWindow?.level = .normal
    }

    private var mainContentWindow: NSWindow? {
        NSApp.windows.first { $0.isVisible && $0.title == "AutomixDemo" }
    }
}

@main
struct AutomixDemoApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: kMinWindowWidth, minHeight: kMinWindowHeight)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: kMinWindowWidth, height: kMinWindowHeight)
    }
}
