// 酒タックチャン用のカスタム口パーツ
//
// 標準の Mouths.hpp にある OmegaMouth / UShapeMouth が、口の描画とは別に
// 画面上の固定オフセット位置へ secondary color で「頬」の楕円を描いている
// 実装(Mouths.cpp)を参考に、
//   1. 呼吸(breath)でゆるく開閉する、だらしない半開きの口
//   2. 標準より一回り大きい赤い頬(ColorPalette の secondary を赤系にして使う)
//   3. ビールのジョッキ(🍺、BeerMug.h のカラー画像)
//      - setBeer() で中身の量と「飲む動作の進み具合」を受け取り、
//        液面の高さを変え、口元へ持ち上げて傾ける
// をまとめて描画する。
//
// 座標や大きさは実機の画面で見ながら調整する前提の初期値。

#ifndef DRUNK_MOUTH_H_
#define DRUNK_MOUTH_H_

#include <math.h>

#include <Mouths.hpp>

#include "BeerMug.h"

namespace m5avatar {

class DrunkMouth : public BaseMouth {
 private:
  // ジョッキを合成するための小さなスプライト(初回描画時に確保する)
  M5Canvas mug_{&M5.Lcd};

  // loop() 側のタスクが書き、描画タスクが読む。float の読み書きは
  // ESP32 では1命令なので、volatile だけで足りる。
  volatile float beer_level_ = 1.0f;  // 中身の量 0.0(空) - 1.0(満タン)
  volatile float drink_ = 0.0f;       // 0.0(机の上) - 1.0(口元で傾けている)

  bool ensureMugSprite() {
    if (mug_.getBuffer() != nullptr) return true;
    mug_.setColorDepth(16);
    mug_.createSprite(kMugSize, kMugSize);
    return mug_.getBuffer() != nullptr;
  }

  // ジョッキ(まっすぐ立てた状態)を mug_ に合成する。重ね順は
  //   内側の薄い色 -> ビール液(液面より下) -> 泡 -> ガラス(縁・取っ手・ハイライト)
  // で、BeerMug.h の画像は uint16_t* が swap565 として読まれ、
  // 透過キーは uint16_t の 0 を渡す必要がある(M5GFX の create_pc_tr 参照)。
  void renderMug(float level) {
    const uint16_t key = 0;
    const uint16_t tint = mug_.color565(34, 42, 52);
    const uint16_t cream = mug_.color565(255, 244, 214);

    mug_.fillSprite(key);

    for (int y = kMugLiquidTop; y < kMugLiquidBottom; ++y) {
      if (kMugRowMax[y] >= kMugRowMin[y]) {
        mug_.fillRect(kMugRowMin[y], y, kMugRowMax[y] - kMugRowMin[y] + 1, 1,
                      tint);
      }
    }

    if (level > 0.01f) {
      // 液面の行。level が減るほど下がる。
      int surface = kMugLiquidBottom -
                    static_cast<int>(lroundf(level * (kMugLiquidBottom -
                                                      kMugLiquidTop)));
      if (surface < kMugLiquidBottom) {
        // 行優先の画像なので、液面以降の行だけをポインタをずらして描く
        mug_.pushImage(0, surface, kMugSize, kMugSize - surface,
                       kMugLiquid + surface * kMugSize, key);
      }
      if (level >= 0.85f) {
        mug_.pushImage(0, 0, kMugSize, kMugSize, kMugFoam, key);  // 泡の山
      } else {
        // 泡の山は消えて、液面に薄い泡が残るだけにする
        for (int y = surface; y < surface + 3 && y < kMugLiquidBottom; ++y) {
          if (kMugRowMax[y] >= kMugRowMin[y]) {
            mug_.fillRect(kMugRowMin[y], y, kMugRowMax[y] - kMugRowMin[y] + 1,
                          1, cream);
          }
        }
      }
    }

    mug_.pushImage(0, 0, kMugSize, kMugSize, kMugGlass, key);
  }

  // 口元(center_x_, center_y_)を基準にジョッキを描く
  void drawMug(M5Canvas *canvas) {
    if (!ensureMugSprite()) return;

    float d = drink_;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;

    // 机の上(右頬の下)の位置と、口元で傾けたときの位置。どちらもジョッキ中心。
    const float rest_dx = 108.0f, rest_dy = 17.0f;
    const float drink_dx = 64.0f, drink_dy = 11.0f;
    const float tilt_deg = -65.0f;  // マイナスで反時計回り(縁が口のある左を向く)

    float x = center_x_ + rest_dx + (drink_dx - rest_dx) * d;
    float y = center_y_ + rest_dy + (drink_dy - rest_dy) * d;

    renderMug(beer_level_);
    mug_.pushRotateZoom(canvas, x, y, tilt_deg * d, 1.0f, 1.0f,
                        static_cast<uint16_t>(0));
  }

 public:
  using BaseMouth::BaseMouth;

  // level: 中身の量(0.0-1.0) / drink: 飲む動作の進み具合(0.0-1.0)
  void setBeer(float level, float drink) {
    beer_level_ = level;
    drink_ = drink;
  }

  void draw(M5Canvas *canvas, BoundingRect rect, DrawContext *ctx) override {
    this->update(canvas, rect, ctx);  // primary/secondary/background 等を取得

    // だらしなく緩んだ半開きの口。呼吸(breath_)ぶんだけ開閉が揺れる。
    float breathed_ratio = open_ratio_ + breath_ * 0.15f;
    if (breathed_ratio < 0.0f) breathed_ratio = 0.0f;
    if (breathed_ratio > 1.0f) breathed_ratio = 1.0f;
    uint16_t h = min_height_ + (max_height_ - min_height_) * breathed_ratio;
    uint16_t w =
        min_width_ + (max_width_ - min_width_) * (1.0f - breathed_ratio);
    canvas->fillEllipse(center_x_, center_y_, w / 2, h / 2, primary_color_);

    // 赤く火照った頬(標準の頬より一回り大きく、secondary色=赤で描く)
    const int16_t cheek_dx = 130;
    const int16_t cheek_dy = -23;
    const int16_t cheek_rx = 30;
    const int16_t cheek_ry = 16;
    canvas->fillEllipse(center_x_ - cheek_dx, center_y_ + cheek_dy, cheek_rx,
                        cheek_ry, secondary_color_);
    canvas->fillEllipse(center_x_ + cheek_dx, center_y_ + cheek_dy, cheek_rx,
                        cheek_ry, secondary_color_);

    // 口より後に描くので、口元で傾けたときはジョッキが口の上に重なる
    drawMug(canvas);
  }
};

}  // namespace m5avatar

#endif  // DRUNK_MOUTH_H_
