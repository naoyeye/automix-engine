import SwiftUI
import Automix

@main
struct AutomixDemoApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: kMinWindowWidth, minHeight: kMinWindowHeight)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: kMinWindowWidth, height: kMinWindowHeight)
    }
}
