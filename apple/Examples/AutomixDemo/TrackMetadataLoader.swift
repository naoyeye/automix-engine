//
//  TrackMetadataLoader.swift
//  AutomixDemo
//
//  从音频文件路径加载元数据（艺术家、专辑、封面等），使用 AVFoundation。
//

import Foundation
import AVFoundation
import AppKit

/// 从音频文件提取的元数据（艺术家、专辑、封面等）
struct TrackMetadata {
    let title: String?
    let artist: String?
    let album: String?
    let artwork: NSImage?
}

enum TrackMetadataLoader {
    /// 从文件路径异步加载元数据
    static func loadMetadata(from path: String) async -> TrackMetadata {
        let url = URL(fileURLWithPath: path)
        let asset = AVURLAsset(url: url)
        
        var title: String?
        var artist: String?
        var album: String?
        var artwork: NSImage?
        
        do {
            let metadata = try await asset.load(.commonMetadata)
            for item in metadata {
                if let key = item.commonKey?.rawValue {
                    switch key {
                    case AVMetadataKey.commonKeyTitle.rawValue:
                        title = item.stringValue
                    case AVMetadataKey.commonKeyArtist.rawValue:
                        artist = item.stringValue
                    case AVMetadataKey.commonKeyAlbumName.rawValue:
                        album = item.stringValue
                    case AVMetadataKey.commonKeyArtwork.rawValue:
                        if let data = item.dataValue {
                            artwork = NSImage(data: data)
                        }
                    default:
                        break
                    }
                }
            }
        } catch {
            // 记录加载失败原因，有助于调试（权限问题、格式不支持等）
            print("Failed to load metadata for \(path): \(error)")
        }
        
        return TrackMetadata(title: title, artist: artist, album: album, artwork: artwork)
    }
}
