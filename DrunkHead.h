// 酒タックチャン用の首の動き
//
// M5StackChan (CoreS3 ベースの公式スタックチャン) の首は、StackChan-BSP の
// M5StackChan.Motion で動かす(サーボは SCS0009 のシリアルバスサーボ)。
// 角度の単位は 0.1° で、
//   yaw  (左右): -1280〜1280。プラスがスタックチャンから見て左(画面に向かって右)
//   pitch(上下): 0〜900。0 がホーム(正面)で、プラスにすると上を向く。
//                サーボの焼き付き防止のため 50〜850 の範囲で使う(公式ドキュメント)
// 速さは 0〜1000 で、BSP 側でバネの硬さに変換されてなめらかに加減速する。
//
// DrunkHead は「キーフレームの列」を受け取り、各キーの保持時間ごとに次の
// キーを Motion へ送るだけの小さな再生器。
// BSP は停止中に自動でトルクを抜くので、上を向いてジョッキを傾けている間に
// 首が重さで垂れてしまう。そこで休む姿勢へ戻るときだけ自動トルク解除を有効にする。

#ifndef DRUNK_HEAD_H_
#define DRUNK_HEAD_H_

#include <stddef.h>

#include <M5StackChan.h>

struct HeadKey {
  int16_t yaw;       // 0.1°
  int16_t pitch;     // 0.1°
  int16_t speed;     // 0-1000
  uint16_t hold_ms;  // 次のキーへ進むまでの時間
};

class DrunkHead {
 private:
  int16_t rest_yaw_;
  int16_t rest_pitch_;

  const HeadKey *keys_ = nullptr;  // 再生中の列 (nullptr なら停止中)
  size_t num_keys_ = 0;
  size_t index_ = 0;
  unsigned long next_at_ = 0;

 public:
  DrunkHead(int16_t rest_yaw, int16_t rest_pitch)
      : rest_yaw_(rest_yaw), rest_pitch_(rest_pitch) {}

  void play(const HeadKey *keys, size_t num_keys, unsigned long now) {
    keys_ = keys;
    num_keys_ = num_keys;
    index_ = 0;
    next_at_ = now;
    update(now);
  }

  template <size_t N>
  void play(const HeadKey (&keys)[N], unsigned long now) {
    play(keys, N, now);
  }

  void update(unsigned long now) {
    if (keys_ == nullptr || now < next_at_) return;

    const HeadKey &k = keys_[index_];
    bool at_rest = k.yaw == rest_yaw_ && k.pitch == rest_pitch_;
    M5StackChan.Motion.setAutoTorqueReleaseEnabled(at_rest);
    M5StackChan.Motion.move(k.yaw, k.pitch, k.speed);

    next_at_ = now + k.hold_ms;
    if (++index_ >= num_keys_) keys_ = nullptr;
  }
};

#endif  // DRUNK_HEAD_H_
