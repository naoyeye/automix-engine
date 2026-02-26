import SwiftUI
import Automix

@main
struct AutomixDemoApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 580, minHeight: 500)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 580, height: 500)
    }
}
