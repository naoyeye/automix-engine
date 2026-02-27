#!/bin/bash
# 修复 FFmpeg 无法正确解码的 FLAC 文件
# 方法：ffmpeg -err_detect ignore_err 解码到 WAV，再 re-encode 为 FLAC

set -e
AUDIO_DIR="${1:-audio-files}"
TMP_DIR="$AUDIO_DIR/repair_tmp"
mkdir -p "$TMP_DIR"

repair() {
    local f="$1"
    local base=$(basename "$f" .flac)
    local wav="$TMP_DIR/${base}.wav"
    local rep="$TMP_DIR/${base}_repaired.flac"
    
    echo "Repairing: $base"
    ffmpeg -y -err_detect ignore_err -i "$f" -f wav "$wav" 2>/dev/null || return 1
    flac -8 -o "$rep" "$wav" 2>/dev/null || return 1
    mv "$rep" "$f"
    rm "$wav"
    echo "  OK"
}

# 需要修复的文件列表（FFmpeg 解码时报 invalid sync code 的）
FILES=(
    "八仙饭店 (8 Immortals Restaurant)-迷津 (Live).flac"
    "八仙饭店 (8 Immortals Restaurant)-单身旅记 (Live).flac"
    "椅子乐团 The Chairs-骑上我心爱的小摩托 (Live).flac"
    "椅子乐团 The Chairs-Rollin' On (Live).flac"
    "刺猬-生之响往 (Live).flac"
    "康姆士乐团-你要如何，我们就如何 (Live).flac"
    "新裤子-别再问我什么是迪斯科 (Live).flac"
    "马赛克乐队-霓虹甜心 (Live).flac"
    "重塑雕像的权利-一生所爱 (Live).flac"
    "新裤子-没有理想的人不伤心 (Live).flac"
    "刺猬-火车驶向云外，梦安魂于九霄 (Live).flac"
    "回春丹乐队-鲜花 (Live).flac"
    "五条人-Last Dance (Live).flac"
    "瓦依那,任素汐-大梦 (Live).flac"
    "五条人,小老虎,大众乐迷-阿珍爱上了阿强 (Live).flac"
    "麻园诗人,万妮达Vinida Weng-榻榻米 (Live).flac"
    "Joyside-太空浪子 (Live).flac"
)

for name in "${FILES[@]}"; do
    f="$AUDIO_DIR/$name"
    if [[ -f "$f" ]]; then
        repair "$f" || echo "  FAILED"
    else
        echo "Skip (not found): $name"
    fi
done

rmdir "$TMP_DIR" 2>/dev/null || true
echo "Done."
