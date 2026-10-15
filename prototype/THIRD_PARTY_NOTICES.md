# 第三者ライセンス・依存関係

確認基準：2026-10-15、公開版 `0ccbd613c017148bfefc347a034e648f8d43d075`。
YMGWorks独自コードと、同一作者の[TheExtend](https://github.com/YMG-CODE/M5stack_TheExtend)由来コードは第三者コードとして扱いません。
TheExtend自体のMIT表示は維持され、そこに含まれる外部依存までYMGWorks名義に置き換えるものではありません。
以下の第三者の著作権表示・利用条件は、独自コードのMITや作品素材の条件に置き換わりません。

## 同梱するソースと、ビルドで取得する依存

| 対象 | 確認した版・証拠 | 条件・配布上の区分 |
|---|---|---|
| M5Unified | 0.2.25、platformio.iniの固定指定と保存済みビルド環境 | MIT。[同梱の文面](LICENSES/M5Unified-MIT.txt)。ソース本体はビルド時に取得 |
| M5GFX | 0.2.32、同上 | MITと内部コンポーネントの条件。[MIT文面](LICENSES/M5GFX-MIT.txt)。ソース本体はビルド時に取得 |
| M5 MAX3421E USB library | library.properties=1.0.0、ローカル改変同梱版。PlatformIO上の表示版と上流の版は同一視しない | MIT。[同梱ライブラリのLICENSE](lib/M5-Max3421E-USBShield/LICENSE)。上流コミットは記録から特定できず、SOURCE_MANIFEST.jsonで配布物を識別 |
| TinyUSB / Adafruit TinyUSB由来部分 | 同梱tusb_option.hは0.19.0。Adafruit側全体のリリースタグは未特定 | 各ソース冒頭のMIT・著作権表示を保持。USBライブラリの一部として同梱 |
| ST STM32 FSDEV定義 | 同梱portable/st/stm32_fsdev/fsdev_stm32.h | 同ヘッダーのBSD系表示を維持。ソースは配布するがESP32用で使うとは限らない |
| Arduino-ESP32 | espressif32 6.12.0が取得する2.0.17（package表示3.20017.241212+sha.dcc1105b） | Arduinoコア部分はLGPL-2.1-or-later。[Arduino.hの条件](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/Arduino.h)。SDK全体をLGPLとみなさない |
| ESP-IDF / SDK | 保存済みESP32-S3 SDKヘッダーは4.4.7 | Apache-2.0および各コンポーネントの条件。配布するバイナリごとにSDKの通知・バイナリ専用部品の条件も確認 |
| 日本語IPA由来フォント | M5GFX lgfxJapanGothic系 | [IPA Font License v1.0](LICENSES/IPA-Font-License-v1.0.txt)。M5GFXのMITだけでは足りない |
| Adafruit GFXフォント | M5GFXのGFXFFフォルダー | [元のBSD系文面](LICENSES/Adafruit-GFX-Fonts.txt)。利用するフォントをバイナリごとに確認 |

上流：[M5Unified](https://github.com/m5stack/M5Unified)、[M5GFX](https://github.com/m5stack/M5GFX)、
[USBライブラリ](https://github.com/m5stack/M5-Max3421E-USBShield)、[Arduino-ESP32](https://github.com/espressif/arduino-esp32)、
[ESP-IDF](https://github.com/espressif/esp-idf/tree/v4.4.7)。

M5GFX内の各ファイル（PNG/JPEGデコード等を含む）の表示も維持してください。
この表はSDK・ツールチェーン全構成要素を網羅したSBOMではありません。

## 実際の組込みの確認範囲

公開用コピーの2026-10-11ビルド記録と、保存済みCoreS3のELF・mapを参照しました。
ELFの定義シンボルに `hcd_init`、`tuh_hid_receive_report`、`lgfx_font_japan_gothic_12`、
`lgfxJapanGothic_12` を確認し、USBホストと当該日本語フォントが単なる宣言依存ではないことを確認しています。
M5Unified/M5GFX/Arduinoのオブジェクトもmapにあります。
mapの入力・破棄セクションに名前があるだけで、すべてを最終バイナリへ組み込んだとは判断しません。
ST定義や全フォント等の同梱ファイルと、実際にリンクされたものは区別します。
本リポジトリにはファームウェアを同梱しておらず、今回の文書修正で新規ビルドはしていません。

## ローカル改変と来歴

USBライブラリにはMAX3421E IRQ処理、SPI排他・診断、HID処理の開発版からの変更が含まれます。
元の著作権表示は保持しています。上流の正確な基準コミットと改変一覧は今後固定する必要があります。
TheExtendのPC用TypingBridgeやその依存を、Napetuneファームウェアの依存として自動的に引き継ぐものではありません。

## 頒布時の対応

現状はソース公開です。書込み済み製品・ファームウェアを頒布する場合の通知、LGPL対応、フォント等の確認は、
[頒布チェックと未解決事項](docs/PUBLIC_ANNOUNCEMENT_AUDIT.md)に集約しています。
ライブラリやフォントの条件は、作品素材の利用許諾とは別に対応してください。

## 旧エンジン

公開用コピーにはPrBoom、PrBoom互換コード、WADテーブル、旧ESP-IDF用components、Freedoom WADを収録していません。
過去に使用した事実だけで現在の独自コードをGPLと判断しません。他方、実際に第三者から移植したコードが見つかった場合は個別に条件を確認します。
