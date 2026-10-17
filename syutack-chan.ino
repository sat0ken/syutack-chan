// 酒タックチャン
// いつもビールを飲んで酔っ払っている顔を表示し続けるスタックチャン
//
// 対象ハード: M5StackChan (スイッチサイエンス K151 / CoreS3 ベース)
// 必要ライブラリ: M5Stack_Avatar (v0.10.0系), StackChan-BSP,
//                M5Unified, M5GFX (0.2.27 以上。K151 の LCD ドライバ変更に対応)
//
// 表現方針(詳細は実装プラン参照):
//   - 目 : 自作 DrunkEye (DrunkEye.h) でとろんとした半目を維持し、
//          たまにゆっくりまばたきする
//   - 口・頬 : 自作 DrunkMouth (DrunkMouth.h) でだらしない半開きの口 +
//              赤い頬 + ビールジョッキ(🍺のカラー画像)を描画
//   - 表情 : 通常は Neutral、時々 Sleepy にしてライブラリ内蔵の Zzz
//            エフェクトを「うたた寝」演出として拝借する
//   - ビール: ジョッキを口元へ持ち上げて傾け、一口ごとに中身を減らす。
//            飲み干したら「生おかわり！」と言って満タンに戻す
//   - 首 : 自作 DrunkHead (DrunkHead.h) で、飲む動作に合わせて上を向いたり
//          うなずいたりする
//   - 声 : 吹き出しのセリフを VOICEVOX で読み上げた音声 (Voices.h、
//          tools/make_voice.py で生成) を鳴らし、音量に合わせて口パクする。
//          起動時は「今日も元気だ！」「ビールがうまい！」とあいさつしてから
//          飲み始める。音声: VOICEVOX:ずんだもん
//
// 呼吸によるゆらぎ・視線のランダムな揺らぎ(サッケード)はライブラリが
// 内部タスクで自動的に行っているので、ここでは自前実装しない。

#include <M5Unified.h>
#include <M5StackChan.h>
#include <Avatar.h>
#include <Eyes.hpp>
#include <Mouths.hpp>

#include "DrunkEye.h"
#include "DrunkHead.h"
#include "DrunkMouth.h"
#include "Voices.h"

using namespace m5avatar;

Avatar avatar;

// ---- とろん目 & まばたきステートマシン ----
const float kDroopyOpenRatio = 0.35f;  // 平常時の開眼率(半目)

enum class BlinkPhase { RestingDroopy, Closing, Closed, Opening };
BlinkPhase blinkPhase = BlinkPhase::RestingDroopy;
unsigned long blinkPhaseStart = 0;
unsigned long nextBlinkAt = 0;

void updateBlink(unsigned long now) {
  switch (blinkPhase) {
    case BlinkPhase::RestingDroopy:
      avatar.setEyeOpenRatio(kDroopyOpenRatio);
      if (now >= nextBlinkAt) {
        blinkPhase = BlinkPhase::Closing;
        blinkPhaseStart = now;
      }
      break;

    case BlinkPhase::Closing: {
      const unsigned long dur = 120;
      float t = min(1.0f, (now - blinkPhaseStart) / (float)dur);
      avatar.setEyeOpenRatio(kDroopyOpenRatio * (1.0f - t));
      if (t >= 1.0f) {
        blinkPhase = BlinkPhase::Closed;
        blinkPhaseStart = now;
      }
      break;
    }

    case BlinkPhase::Closed:
      if (now - blinkPhaseStart >= 90) {
        blinkPhase = BlinkPhase::Opening;
        blinkPhaseStart = now;
      }
      break;

    case BlinkPhase::Opening: {
      const unsigned long dur = 150;
      float t = min(1.0f, (now - blinkPhaseStart) / (float)dur);
      avatar.setEyeOpenRatio(kDroopyOpenRatio * t);
      if (t >= 1.0f) {
        blinkPhase = BlinkPhase::RestingDroopy;
        nextBlinkAt = now + random(2500, 5000);
      }
      break;
    }
  }
}

// ---- 時々うたた寝(Sleepy)してZzzエフェクトを出す ----
bool inSleepy = false;
unsigned long nextSleepyAt = 0;
unsigned long sleepyUntil = 0;

void updateSleepy(unsigned long now) {
  if (inSleepy) {
    if (now >= sleepyUntil) {
      avatar.setExpression(Expression::Neutral);
      inSleepy = false;
      nextSleepyAt = now + random(6000, 12000);
    }
  } else if (now >= nextSleepyAt) {
    avatar.setExpression(Expression::Sleepy);
    inSleepy = true;
    sleepyUntil = now + random(1200, 2200);
  }
}

// ---- 吹き出し & 声 ----
// say() に渡した文字列と同じセリフが Voices.h にあれば、吹き出しと一緒に
// その声を鳴らし、声の音量に合わせて口をパクパクさせる。
const float kMouthRestRatio = 0.35f;  // 普段のうっすら開けた口
const uint8_t kVoiceVolume = 180;     // スピーカーの音量 0-255

unsigned long speechClearAt = 0;           // 0 なら表示していない
const VoiceClip *playingVoice = nullptr;  // nullptr なら声を出していない
unsigned long voiceStartAt = 0;

bool isVoicePlaying() { return playingVoice != nullptr; }

void say(const char *text, unsigned long now, unsigned long durMs) {
  avatar.setSpeechText(text);

  playingVoice = nullptr;
  for (uint8_t i = 0; i < kNumVoiceClips; ++i) {
    if (strcmp(kVoiceClips[i].text, text) == 0) {
      playingVoice = &kVoiceClips[i];
      break;
    }
  }
  if (playingVoice != nullptr) {
    M5.Speaker.playRaw(playingVoice->pcm, playingVoice->samples,
                       kVoiceSampleRate, false, 1, 0, true);
    voiceStartAt = now;
    // しゃべり終わる前に吹き出しが消えないようにする
    unsigned long voiceMs = playingVoice->frames * kVoiceFrameMs;
    if (durMs < voiceMs + 300) durMs = voiceMs + 300;
  }

  speechClearAt = now + durMs;
}

void updateSpeech(unsigned long now) {
  if (playingVoice != nullptr) {
    unsigned long frame = (now - voiceStartAt) / kVoiceFrameMs;
    if (frame < playingVoice->frames) {
      avatar.setMouthOpenRatio(0.15f +
                               0.85f * playingVoice->env[frame] / 255.0f);
    } else {
      avatar.setMouthOpenRatio(kMouthRestRatio);
      playingVoice = nullptr;
    }
  }

  if (speechClearAt != 0 && now >= speechClearAt) {
    avatar.setSpeechText("");
    speechClearAt = 0;
  }
}

// ---- ビールを飲む動作 ----
// 待つ -> ジョッキを口元へ持ち上げて傾ける -> 一口飲む(中身が減る) ->
// ジョッキを戻す -> 「ぷはー」 を繰り返し、飲み干したら
// 「生おかわり！」 -> 満タンに戻る -> 「かんぱーい！」
const int kSipsPerMug = 6;  // 1杯を何口で飲み干すか
const char *kSipPhrases[] = {"ぷはー", "うまい！", "きゅ〜っ"};
const uint8_t kNumSipPhrases = sizeof(kSipPhrases) / sizeof(kSipPhrases[0]);

enum class DrinkPhase { Idle, Lift, Sip, Lower, Emptied, Refill };
DrinkPhase drinkPhase = DrinkPhase::Idle;
unsigned long drinkPhaseStart = 0;
unsigned long idleWaitMs = 0;

float beerLevel = 1.0f;      // 中身の量 1.0=満タン 0.0=空
float sipStartLevel = 1.0f;  // 一口飲み始めたときの量
float drinkLift = 0.0f;      // 0.0=机の上 1.0=口元で傾けている

DrunkMouth *drunkMouth = nullptr;  // setup() で生成し、ジョッキの状態を渡す

// ---- 飲む動作に合わせた首の動き ----
// {yaw, pitch, 速さ, 次のキーまでの時間ms}。角度は 0.1° 単位 (DrunkHead.h 参照)。
// ジョッキは画面の右側 = スタックチャンから見て左手に持っている。
const int16_t kHeadRestPitch = 100;  // 普段はほんの少し上を向いておく
DrunkHead head(0, kHeadRestPitch);

const HeadKey kHeadRest[] = {{0, kHeadRestPitch, 300, 0}};
// ジョッキの方へ少し首をかしげながら持ち上げる
const HeadKey kHeadLift[] = {{80, 250, 350, 0}};
// 飲みながらゆっくり上を向いて、ジョッキをあおる
const HeadKey kHeadSip[] = {{40, 450, 200, 0}};
const HeadKey kHeadLower[] = {{0, kHeadRestPitch, 400, 0}};
// 「ぷはー」で一度あごを上げてから戻す
const HeadKey kHeadPuha[] = {{0, 250, 700, 300}, {0, kHeadRestPitch, 400, 0}};
// 「生おかわり！」で店員さんを探してきょろきょろする
const HeadKey kHeadLookAround[] = {{-200, 250, 450, 700},
                                   {200, 250, 450, 700},
                                   {0, kHeadRestPitch, 400, 0}};
// 「かんぱーい！」でジョッキを掲げるように上を向く
const HeadKey kHeadKanpai[] = {{0, 400, 800, 400}, {0, kHeadRestPitch, 400, 0}};

// 0-1 の進み具合をなめらかな加減速にする
float smooth01(float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

void updateDrink(unsigned long now) {
  const unsigned long elapsed = now - drinkPhaseStart;
  // Arduino が自動生成する関数宣言は enum の定義より前に置かれるため、
  // enum を引数に取る関数は作れない。ラムダなら影響を受けない。
  auto enterDrinkPhase = [now](DrinkPhase phase) {
    drinkPhase = phase;
    drinkPhaseStart = now;
  };

  switch (drinkPhase) {
    case DrinkPhase::Idle:
      // しゃべっている途中で口を開けて飲み始めないようにする
      if (elapsed >= idleWaitMs && !isVoicePlaying()) {
        avatar.setMouthOpenRatio(0.7f);  // 飲むので口を開ける
        head.play(kHeadLift, now);
        enterDrinkPhase(DrinkPhase::Lift);
      }
      break;

    case DrinkPhase::Lift: {
      float t = elapsed / 700.0f;
      drinkLift = smooth01(t);
      if (t >= 1.0f) {
        sipStartLevel = beerLevel;
        head.play(kHeadSip, now);
        enterDrinkPhase(DrinkPhase::Sip);
      }
      break;
    }

    case DrinkPhase::Sip: {
      float t = elapsed / 1400.0f;
      float level = sipStartLevel - smooth01(t) / kSipsPerMug;
      beerLevel = level > 0.0f ? level : 0.0f;
      if (t >= 1.0f) {
        avatar.setMouthOpenRatio(kMouthRestRatio);
        head.play(kHeadLower, now);
        enterDrinkPhase(DrinkPhase::Lower);
      }
      break;
    }

    case DrinkPhase::Lower: {
      float t = elapsed / 700.0f;
      drinkLift = 1.0f - smooth01(t);
      if (t >= 1.0f) {
        drinkLift = 0.0f;
        if (beerLevel <= 0.01f) {
          beerLevel = 0.0f;
          say("生おかわり！", now, 2600);
          head.play(kHeadLookAround, now);
          enterDrinkPhase(DrinkPhase::Emptied);
        } else {
          say(kSipPhrases[random(0, kNumSipPhrases)], now, 1800);
          head.play(kHeadPuha, now);
          idleWaitMs = random(1200, 2600);
          enterDrinkPhase(DrinkPhase::Idle);
        }
      }
      break;
    }

    case DrinkPhase::Emptied:
      if (elapsed >= 2800 && !isVoicePlaying()) {
        enterDrinkPhase(DrinkPhase::Refill);
      }
      break;

    case DrinkPhase::Refill: {
      float t = elapsed / 1500.0f;
      beerLevel = smooth01(t);
      if (t >= 1.0f) {
        beerLevel = 1.0f;
        say("かんぱーい！", now, 1800);
        head.play(kHeadKanpai, now);
        idleWaitMs = random(2000, 3500);
        enterDrinkPhase(DrinkPhase::Idle);
      }
      break;
    }
  }

  if (drunkMouth != nullptr) drunkMouth->setBeer(beerLevel, drinkLift);
}

// しゃべり終わるまで待つ(setup() での起動のあいさつ用)。
// 待っている間も、まばたき・口パク・首の動きは止めない。
void sayAndWait(const char *text) {
  say(text, millis(), 1500);
  while (isVoicePlaying()) {
    M5StackChan.update();
    unsigned long now = millis();
    updateBlink(now);
    updateSpeech(now);
    head.update(now);
    delay(20);
  }
}

void setup() {
  // M5.begin() に加えて、サーボ・RGB LED・タッチセンサ等の本体側を初期化する
  M5StackChan.begin();
  M5.Speaker.setVolume(kVoiceVolume);

  // 引数無しの avatar.init() は既定で1bit(モノクロ)モードになり、
  // ColorPalette の secondary 等を変えても実際の見た目は primary と
  // 同じ色にしかならない(Face.cpp / Eyes.cpp / Mouths.cpp で使用色が
  // colorDepth==1 かどうかで分岐しているのを確認済み)。
  // 頬を本当に赤く見せるため、16bitカラーで初期化する。
  avatar.init(16);

  // 頬を赤くするため、ColorPalette の secondary を赤系にする
  // (primary/background は既定のまま = モノクロのシルエット感を維持)
  ColorPalette cp = avatar.getColorPalette();
  cp.set(COLOR_SECONDARY, M5.Lcd.color565(255, 90, 90));
  avatar.setColorPalette(cp);

  // 吹き出しは日本語を使うので、日本語フォントを指定しておく
  avatar.setSpeechFont(&fonts::lgfxJapanGothicP_16);

  // 目・口を酔っ払い用のパーツに差し替える
  Face *face = avatar.getFace();
  face->setLeftEye(new DrunkEye(true));
  face->setRightEye(new DrunkEye(false));
  drunkMouth = new DrunkMouth(70, 100, 16, 44);
  face->setMouth(drunkMouth);

  // まばたきは自前のステートマシンで管理するので内蔵の自動まばたきは止める
  avatar.setIsAutoBlink(false);
  avatar.setEyeOpenRatio(kDroopyOpenRatio);

  // 口は常にうっすら開けておく(呼吸ぶんはDrunkMouth側で追加で揺れる)
  avatar.setMouthOpenRatio(kMouthRestRatio);

  // バッテリーアイコンは非表示にして画面をすっきりさせる
  avatar.setBatteryIcon(false);

  avatar.setExpression(Expression::Neutral);

  unsigned long now = millis();
  nextBlinkAt = now + random(2500, 5000);
  drunkMouth->setBeer(beerLevel, drinkLift);
  head.play(kHeadRest, now);

  // 起動のあいさつ。言い終わってからメインループ(飲む動作)に入る。
  // 1つの吹き出しには収まらない長さなので、2つに分けて続けて言う。
  sayAndWait("今日も元気だ！");
  sayAndWait("ビールがうまい！");

  now = millis();
  nextSleepyAt = now + random(6000, 12000);
  idleWaitMs = random(1500, 3000);
  drinkPhaseStart = now;
}

void loop() {
  M5StackChan.update();

  unsigned long now = millis();
  updateBlink(now);
  updateSleepy(now);
  updateDrink(now);
  updateSpeech(now);
  head.update(now);

  delay(20);
}
