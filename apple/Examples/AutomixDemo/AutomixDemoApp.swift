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
        w.makeKeyAndOrderFront(nil)
    }

    private var mainContentWindow: NSWindow? {
        NSApp.keyWindow
            ?? NSApp.mainWindow
            ?? NSApp.windows.first { $0.isVisible }
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
