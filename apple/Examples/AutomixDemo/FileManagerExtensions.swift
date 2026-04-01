import Foundation

extension FileManager {
    /// Returns (creating if needed) a named subdirectory inside the user's Application Support folder.
    func appSupportSubdirectory(_ name: String, caller: String = #fileID) -> URL? {
        do {
            let appSupport = try url(
                for: .applicationSupportDirectory,
                in: .userDomainMask,
                appropriateFor: nil,
                create: true
            )
            let dir = appSupport.appendingPathComponent(name, isDirectory: true)
            try createDirectory(at: dir, withIntermediateDirectories: true)
            return dir
        } catch {
            print("[\(caller)] resolve app support subdirectory failed (\(name)): \(error.localizedDescription)")
            return nil
        }
    }
}
