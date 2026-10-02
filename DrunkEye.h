// 酒タックチャン用のカスタム目パーツ
//
// M5Stack-Avatar 標準の Eye / EllipseEye は開眼率(openRatio)が
// 「0 なら閉じた線、0 より大きければ常に全開の丸」という二値の挙動しかせず、
// 開眼率を 0.3〜0.4 に固定しても見た目は普通の全開の目のままになってしまう
// (Eye.cpp / Eyes.cpp を実際に読んで確認済み)。
//
// 「とろん」とした半目を連続的な開眼率で表現するため、Eyes.hpp の BaseEye を
// 継承し、上まぶたを開眼率に応じて連続的に下ろすマスクを自前で描画する。
// 色は primary/background のみを使い、スタックチャンらしいモノクロの
// シルエット感を崩さないようにしている。

#ifndef DRUNK_EYE_H_
#define DRUNK_EYE_H_

#include <Eyes.hpp>

namespace m5avatar {

class DrunkEye : public BaseEye {
 public:
  using BaseEye::BaseEye;

  void draw(M5Canvas *canvas, BoundingRect rect, DrawContext *ctx) override {
    this->update(canvas, rect, ctx);

    // Sleepy表情のときは他の目パーツと同じく完全に閉じた線にする
    // (居眠り演出中はこちらが優先される)
    if (open_ratio_ <= 0.02f || expression_ == Expression::Sleepy) {
      canvas->fillRect(shifted_x_ - width_ / 2, shifted_y_ - 2 + height_ / 4,
                        width_, 4, primary_color_);
      return;
    }

    // 黒目本体(だ円のシルエット)
    canvas->fillEllipse(shifted_x_, shifted_y_, width_ / 2, height_ / 2,
                        primary_color_);

    // まぶた: 開眼率が低いほど上から連続的に多く覆う(＝とろん目)
    int16_t eye_top = shifted_y_ - height_ / 2;
    int16_t lid_h = static_cast<int16_t>((1.0f - open_ratio_) * height_);
    if (lid_h > 0) {
      canvas->fillRect(shifted_x_ - width_ / 2 - 2, eye_top - 2, width_ + 4,
                        lid_h + 2, background_color_);
    }
  }
};

}  // namespace m5avatar

#endif  // DRUNK_EYE_H_
