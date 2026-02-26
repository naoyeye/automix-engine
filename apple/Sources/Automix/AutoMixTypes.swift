//
//  AutoMixTypes.swift
//  Automix
//
//  Created by Swift API Design Plan.
//

import Foundation

/// 错误类型，映射 C API 的 `AutoMixError`
public enum AutoMixError: Error {
    case invalidArgument
    case fileNotFound
    case decodeFailed
    case analysisFailed
    case databaseError
    case playbackError
    case outOfMemory
    case notInitialized
    case unknown(Int)
    
    // 假设内部有一个从 C Int 映射到此枚举的方法
    // static func from(code: Int32) -> AutoMixError
}

/// 播放状态，映射 C API 的 `AutoMixPlaybackState`
public enum AutoMixPlaybackState: Int {
    case stopped = 0
    case playing = 1
    case paused = 2
    case transitioning = 3
}

/// 曲目信息，映射 C API 的 `AutoMixTrackInfo`
public struct TrackInfo {
    public let id: Int64
    public let path: String
    public let bpm: Float
    /// Camelot 音调表示法, 例如 "8A"
    public let key: String
    /// 持续时间（秒）
    public let duration: Float
    /// 分析时的 Unix 时间戳
    public let analyzedAt: Int64
    
    public init(id: Int64, path: String, bpm: Float, key: String, duration: Float, analyzedAt: Int64) {
        self.id = id
        self.path = path
        self.bpm = bpm
        self.key = key
        self.duration = duration
        self.analyzedAt = analyzedAt
    }
}

/// 播放列表生成规则，映射 C API 的 `AutoMixPlaylistRules`
public struct PlaylistRules {
    /// 最大 BPM 差异 (0.0 = 不限制)
    public var bpmTolerance: Float = 0.0
    /// 是否允许改变音调
    public var allowKeyChange: Bool = true
    /// 最大的 Camelot 轮盘距离 (0 = 不限制)
    public var maxKeyDistance: Int = 0
    /// 最小能量相似度 (0.0-1.0)
    public var minEnergyMatch: Float = 0.0
    /// 风格过滤器，nil 表示任何风格
    public var styleFilter: [String]? = nil
    /// 是否允许混合不同风格
    public var allowCrossStyle: Bool = true
    /// 随机种子，用于生成可复现的播放列表 (0 = 非确定性)
    public var randomSeed: UInt32 = 0
    
    public init() {}
}

/// 过渡配置，映射 C API 的 `AutoMixTransitionConfig`
public struct TransitionConfig {
    /// 交叉淡入淡出的节拍数 (默认: 16)
    public var crossfadeBeats: Float = 16.0
    /// 是否使用基于 EQ 的过渡
    public var useEqSwap: Bool = false
    /// 最大时间拉伸比例 (例如: 0.06 表示 ±6%)
    public var stretchLimit: Float = 0.06
    /// 过渡后平滑恢复拉伸至 1.0 的时间（秒）
    public var stretchRecoverySeconds: Float = 5.0
    
    public init() {}
}
