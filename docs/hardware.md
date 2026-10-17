# 対応ハードウェア・動作確認

## 現在の配布構成

**CoreS3 Lite用**と**Core2用デモ版（USB CS＝G19）**を配布しています。
Core2版はデモとして提供し、今後はCoreS3 Liteを主体に開発します。
[ダウンロード・機種別の対応ソース](firmware.md)を参照してください。

## 状態の読み方

**確認済み**は対象の版・構成で実機確認された項目、**未検証**は実機結果が記録されていない項目、
**制約あり**は利用上の条件や既知の制約がある構成です。ビルド成功を実機確認とは扱いません。
構成の状態と個々の確認結果は別々に示します。

| 構成・環境 | ビルド | 実機確認 | 構成の状態 |
|---|---|---|---|
| CoreS3 Lite / m5stack-cores3-lite-app | 確認済み | アプリ移植初版は確認済み。配布バイナリは未検証 | 制約あり：モニターのDTR/RTS設定 |
| Core2用デモ版 / m5stack-core2-usb-cs19 | 確認済み | 作者の使用構成はG19。配布バイナリは未検証 | 制約あり：USB CSの物理経路をG19に合わせる |

現在の配布物のビルド・再リンク結果とソースの識別情報は[リリース説明](https://github.com/YMG-CODE/napetune/releases/tag/v0.1.0-prototype.2)に記載しています。
開発時の実機確認と、配布バイナリの実機確認は区別しています。
[実機・ケース写真](gallery.md)と[システム構成・接続図](system.md)も参照してください。

## 必要な機材

- CoreS3 Lite（8MB QSPI PSRAM）またはCore2。
- USB Module v1.2 / MAX3421EとNape Proの対応USBドングル。
- GLASS2 Unit（任意）、microSD（保存・外部素材用）。

下表は**本体でプログラムが使うGPIO番号**であり、USBモジュールのCS/INTスイッチ印字とは別です。
同じM-BUS位置でも本体のGPIO番号は異なります。印字だけを見てスイッチを変更しないでください。
現在正常に動いている構成は保持し、初めて組む場合は[USBモジュール公式資料](https://docs.m5stack.com/en/module/Module_USB_v1.2)と
[CoreS3公式資料](https://docs.m5stack.com/en/core/CoreS3)で対応を確認します。
接続図のGPIO表記とモジュールのスイッチ印字は区別してください。

| 信号 | CoreS3 Lite | Core2用デモ版（G19） |
|---|---|---|
| SPI SCK / MOSI / MISO | G36 / G37 / G35 | G18 / G23 / G38 |
| USB CS / INT | G7 / G10 | G19 / G35 |
| SD CS | G4 | G4 |
| GLASS2 SDA / SCL | G2 / G1 | G32 / G33 |

Core2用デモ版はUSB CS＝G19です。モジュールの物理CS経路も、この構成に合わせてください。
コードの環境選択だけではモジュールの物理経路は切り替わりません。
リポジトリ内に残るG33環境などの説明は[従来の共通ソース構成（参考）](getting-started.md#従来の共通ソース構成参考)に分けています。

## 再利用できる制約・回避方法

### CoreS3 Liteのシリアルモニター

CoreS3 Liteでは、DTR/RTSをOFFにしたPlatformIOモニターを使用してください。
起動コマンドと終了方法は[導入方法](getting-started.md)を参照してください。

### 画面下の仮想ボタン

CoreS3の最下部20pxは左・中央・右がA/B/Cです。Nape操作と既存のタッチメニューも使用します。
