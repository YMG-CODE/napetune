# 第三者ライセンス・クレジット

Napetuneは次のライブラリ・フォントを利用しています。
第三者のソース内の著作権表示・ライセンス文は維持し、YMGWorks独自コードのMITや作品素材の条件とは分けて扱います。

下表はこの`prototype/`の共通ソースに対応します。別添ソースから作成するCore2用デモ版はM5Unified 0.2.23／M5GFX 0.2.30を使用し、
その対応ソース・通知・再リンク資料をリリースに添付しています。[機種別の対応ソース](../docs/firmware.md)を参照してください。

| 対象 | 使用版・構成 | ライセンス・通知 |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | 0.2.25 | [MIT](LICENSES/M5Unified-MIT.txt) |
| [M5GFX](https://github.com/m5stack/M5GFX) | 0.2.32 | [MIT](LICENSES/M5GFX-MIT.txt)および内部コンポーネントの条件 |
| [M5 MAX3421E USB library](https://github.com/m5stack/M5-Max3421E-USBShield) | 同梱改変版（library.properties: 1.0.0） | [MIT](lib/M5-Max3421E-USBShield/LICENSE)。同梱ソースの表示を維持 |
| TinyUSB / Adafruit TinyUSB由来コード | USBライブラリに同梱。TinyUSB内部表記0.19.0 | 各ソース冒頭のMIT・著作権表示 |
| ST STM32 FSDEV定義 | 同梱USBソース内の別機種向け定義 | [元のBSD系表示](lib/M5-Max3421E-USBShield/src/portable/st/stm32_fsdev/fsdev_stm32.h) |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32/tree/2.0.17) | 2.0.17（espressif32 6.12.0が取得） | コア部分は[LGPL-2.1-or-later](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/Arduino.h)。SDKは各構成要素の条件 |
| [ESP-IDF / SDK](https://github.com/espressif/esp-idf/tree/v4.4.7) | Arduino-ESP32付属SDKの4.4.7 | Apache-2.0および各構成要素のライセンス |
| 日本語IPA由来フォント | M5GFX lgfxJapanGothic系 | [IPA Font License v1.0](LICENSES/IPA-Font-License-v1.0.txt) |
| Adafruit GFXフォント | M5GFXのGFXFFフォルダー | [元のBSD系表示](LICENSES/Adafruit-GFX-Fonts.txt) |

## 配布するものとビルド用の依存

このリポジトリでは対応ファームウェアのソースを公開しています。ファームウェアバイナリはソースツリーに同梱せず、
[GitHub Releases](https://github.com/YMG-CODE/napetune/releases)で配布します。対応ソース・使用ライブラリ・再リンク資料は同じリリースに添付します。
USBライブラリはソースを同梱し、M5Unified・M5GFX・Arduino-ESP32等はPlatformIOがビルド時に取得します。
同梱ソースには他機種用の定義も含まれます。全ファイルが各ボードのファームウェアへ組み込まれるわけではありません。
使用するライブラリ・SDK・フォントの各ファイルに記載された通知も維持してください。

## 同梱USBライブラリの変更

MAX3421EのIRQ処理、SPI排他・診断、HID処理にNapetune向けの変更を含みます。
元の著作権表示・ライセンスは保持し、配布ファイルは[SOURCE_MANIFEST.json](SOURCE_MANIFEST.json)で識別します。

## 再配布する場合

ソースやライブラリの再配布では、該当するライセンス文・著作権表示を保持してください。
LGPLライブラリを組み込んだファームウェアを再配布する場合は、無償でも、ライセンス通知・対応するライブラリのソース・
変更したライブラリと再リンクできる資料等を、適用条件に従って提供します。
詳細は[LGPL v2.1（特に第6項）](https://opensource.org/license/lgpl-2-1)を参照してください。
これはコードの利用条件であり、独自のケース・筐体・PCB等の設計データに公開義務を追加するものではありません。

## 独自コードと作品素材

Napetuneと[TheExtend](https://github.com/YMG-CODE/M5stack_TheExtend)は同じ作者であるYMGWorksのプロジェクトです。
独自の対象コードには既存の[MIT](LICENSE)、小説・キャラクター・メカ等には[作品素材の利用条件](CONTENT_LICENSE.md)を適用します。
第三者のライブラリ・フォントの許諾を、作品素材の転載・商用利用許可へ拡張するものではありません。
