#!/usr/bin/env python3
"""吹き出しのセリフを VOICEVOX で読み上げ、M5Unified の Speaker.playRaw() で
鳴らせる 16bit PCM 配列と、口パク用の音量エンベロープとして Voices.h に書き出す。

使い方:
  1. VOICEVOX (アプリ or ENGINE) を起動しておく (既定 http://127.0.0.1:50021)
  2. python3 tools/make_voice.py          (スケッチフォルダで実行)
     python3 tools/make_voice.py --list   (話者とスタイルの一覧を表示)
必要: Python 3 の標準ライブラリのみ
出力: Voices.h
音声を公開・配布するときは VOICEVOX と各キャラクターの規約に従ってクレジットを
表記すること (例: "VOICEVOX:ずんだもん")。
"""
import argparse
import array
import io
import json
import math
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "Voices.h")

# (吹き出しに出す文字列, 読み上げに使うよみ)
# 吹き出しの文字列は syutack-chan.ino の say() に渡す文字列と完全に一致させる
# (一致したセリフだけ声が出る)。セリフを足したらここにも足して再生成する。
PHRASES = [
    ("今日も元気だ！", "今日も元気だ！"),
    ("ビールがうまい！", "ビールがうまい！"),
    ("ぷはー", "ぷはー"),
    ("うまい！", "うまい！"),
    ("きゅ〜っ", "きゅーっ"),
    ("生おかわり！", "なまおかわり！"),
    ("かんぱーい！", "かんぱーい！"),
]

SAMPLE_RATE = 16000  # 実機で鳴らすサンプリングレート
FRAME_MS = 20        # 口パク用エンベロープの1コマの長さ (loop() の周期に合わせる)
PEAK = 0.9           # 各セリフの最大振幅をフルスケールのこの割合にそろえる


def api(base, path, params=None, body=None, method="POST"):
    url = base + path + ("?" + urllib.parse.urlencode(params) if params else "")
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=120) as res:
        return res.read()


def find_style(speakers, name, style):
    for sp in speakers:
        if sp["name"] != name:
            continue
        for st in sp["styles"]:
            if st["name"] == style and st.get("type", "talk") == "talk":
                return st["id"]
    sys.exit("話者 %s / スタイル %s が見つかりません (--list で一覧を確認)" % (name, style))


def synth(base, style_id, yomi, args):
    query = json.loads(api(base, "/audio_query", {"text": yomi, "speaker": style_id}))
    query.update({
        "speedScale": args.speed,
        "pitchScale": args.pitch,
        "intonationScale": args.intonation,
        "prePhonemeLength": 0.05,
        "postPhonemeLength": 0.1,
        "outputSamplingRate": SAMPLE_RATE,
        "outputStereo": False,
    })
    wav = api(base, "/synthesis", {"speaker": style_id}, query)
    with wave.open(io.BytesIO(wav)) as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, SAMPLE_RATE):
            sys.exit("想定外の WAV 形式: ch=%d width=%d rate=%d" % (w.getnchannels(), w.getsampwidth(), w.getframerate()))
        pcm = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        pcm.byteswap()
    return pcm


def normalize(pcm):
    peak = max(1, max(abs(v) for v in pcm))
    k = PEAK * 32767 / peak
    return array.array("h", (int(round(v * k)) for v in pcm))


def frame_rms(pcm):
    n = SAMPLE_RATE * FRAME_MS // 1000
    out = []
    for i in range(0, len(pcm), n):
        chunk = pcm[i:i + n]
        out.append(math.sqrt(sum(v * v for v in chunk) / len(chunk)))
    return out


def c_array(ctype, name, vals, per_line):
    lines = ["  " + ", ".join(str(v) for v in vals[i:i + per_line]) + "," for i in range(0, len(vals), per_line)]
    return "static const %s %s[%d] = {\n%s\n};\n" % (ctype, name, len(vals), "\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="http://127.0.0.1:50021", help="VOICEVOX ENGINE の URL")
    ap.add_argument("--speaker", default="ずんだもん", help="話者名")
    ap.add_argument("--style", default="ヘロヘロ", help="スタイル名")
    ap.add_argument("--speed", type=float, default=0.95, help="話速 (1.0 が標準)")
    ap.add_argument("--pitch", type=float, default=0.0, help="音高 (0.0 が標準)")
    ap.add_argument("--intonation", type=float, default=1.3, help="抑揚 (1.0 が標準)")
    ap.add_argument("--list", action="store_true", help="話者とスタイルの一覧を表示して終了")
    args = ap.parse_args()

    try:
        speakers = json.loads(api(args.url, "/speakers", method="GET"))
    except urllib.error.URLError as e:
        sys.exit("VOICEVOX ENGINE (%s) に接続できません: %s" % (args.url, e))

    if args.list:
        for sp in speakers:
            styles = ", ".join("%s(%d)" % (st["name"], st["id"]) for st in sp["styles"] if st.get("type", "talk") == "talk")
            print("%s: %s" % (sp["name"], styles))
        return

    style_id = find_style(speakers, args.speaker, args.style)
    clips = []
    for text, yomi in PHRASES:
        pcm = normalize(synth(args.url, style_id, yomi, args))
        clips.append((text, pcm, frame_rms(pcm)))
        print("%s: %.2f s" % (text, len(pcm) / SAMPLE_RATE))

    # 口の開き具合 0-255。全セリフ共通の最大音量を基準にし、小さな音でも
    # 口が動いて見えるよう少し持ち上げる (0.6 乗)。ごく小さい音は閉じる。
    loudest = max(max(rms) for _, _, rms in clips)

    def mouth(rms):
        r = rms / loudest
        return 0 if r < 0.04 else min(255, int(round(255 * r ** 0.6)))

    credit = "VOICEVOX:%s" % args.speaker
    with open(OUT_H, "w") as f:
        f.write("// 吹き出しのセリフを読み上げた %d Hz / 16bit / モノラルの PCM と、口パク用の音量エンベロープ。\n" % SAMPLE_RATE)
        f.write("// tools/make_voice.py で生成 (直接編集しない)。音声: %s (スタイル: %s)\n\n" % (credit, args.style))
        f.write("#ifndef VOICES_H_\n#define VOICES_H_\n\n#include <stdint.h>\n\n")
        f.write("static const uint32_t kVoiceSampleRate = %d;\n" % SAMPLE_RATE)
        f.write("static const uint16_t kVoiceFrameMs = %d;  // env の1コマの長さ\n\n" % FRAME_MS)
        f.write("struct VoiceClip {\n")
        f.write("  const char *text;     // 吹き出しの文字列 (say() に渡す文字列と一致させる)\n")
        f.write("  const int16_t *pcm;\n  uint32_t samples;\n")
        f.write("  const uint8_t *env;   // 口の開き具合 0-255、kVoiceFrameMs ごと\n")
        f.write("  uint16_t frames;\n};\n\n")
        for i, (text, pcm, rms) in enumerate(clips):
            f.write("// %s\n" % text)
            f.write(c_array("int16_t", "kVoicePcm%d" % i, pcm.tolist(), 16))
            f.write(c_array("uint8_t", "kVoiceEnv%d" % i, [mouth(v) for v in rms], 25) + "\n")
        f.write("static const VoiceClip kVoiceClips[] = {\n")
        for i, (text, pcm, rms) in enumerate(clips):
            f.write('  {"%s", kVoicePcm%d, %d, kVoiceEnv%d, %d},\n' % (text, i, len(pcm), i, len(rms)))
        f.write("};\n")
        f.write("static const uint8_t kNumVoiceClips = sizeof(kVoiceClips) / sizeof(kVoiceClips[0]);\n\n")
        f.write("#endif  // VOICES_H_\n")
    print("->", os.path.normpath(OUT_H), "(%s)" % credit)


if __name__ == "__main__":
    main()
