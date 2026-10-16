# 対応ハードウェア・動作確認

## 状態の読み方

**確認済み**は対象の版・構成で実機確認された項目、**未検証**は実機結果が記録されていない項目、
**制約あり**は利用上の条件や既知の制約がある構成です。ビルド成功を実機確認とは扱いません。
構成の状態と個々の確認結果は別々に示します。

| 構成・環境 | ビルド | 実機確認 | 構成の状態 |
|---|---|---|---|
| CoreS3 Lite / m5stack-cores3-lite-app | 確認済み | アプリ移植初版は確認済み。公開用コピーは未検証 | 制約あり：モニターのDTR/RTS設定 |
| Core2 / m5stack-core2 | 確認済み | 現行公開版は未検証 | 制約あり：標準USB CS G33とGLASS2 SCL G33の競合 |
| Core2 / m5stack-core2-usb-cs19 | 確認済み | 現行公開版は未検証 | 未検証：CS19構成での実機結果待ち |

公開コピーのビルドの根拠は[既存の記録](../prototype/BUILD_RESULTS.md)です。
CoreS3 Liteの開発時の確認を、公開コピーの全モード・全周辺機器の確認へ拡張しません。
この表の更新に必要な試験と実機写真は[対応機種の確認Issue](https://github.com/YMG-CODE/napetune/issues/5)で管理します。

## 必要な機材

- CoreS3 Lite（8MB QSPI PSRAM）またはCore2。
- USB Module v1.2 / MAX3421EとNapeProの対応USBドングル。
- GLASS2 Unit（任意）、microSD（保存・外部素材用）。

下表は**本体でプログラムが使うGPIO番号**であり、USBモジュールのCS/INTスイッチ印字とは別です。
同じM-BUS位置でも本体のGPIO番号は異なります。印字だけを見てスイッチを変更しないでください。
現在正常に動いている構成は保持し、初めて組む場合は[USBモジュール公式資料](https://docs.m5stack.com/en/module/Module_USB_v1.2)と
[CoreS3公式資料](https://docs.m5stack.com/en/core/CoreS3)で対応を確認します。
現物のCS印字の対応は、写真を含めて上記Issueで確認中です。

| 信号 | CoreS3 Lite | Core2標準 |
|---|---|---|
| SPI SCK / MOSI / MISO | G36 / G37 / G35 | G18 / G23 / G38 |
| USB CS / INT | G7 / G10 | G33 / G35 |
| SD CS | G4 | G4 |
| GLASS2 SDA / SCL | G2 / G1 | G32 / G33 |

Core2標準のUSB CSとGLASS2 SCLはG33で競合します。GLASS2併用時はUSB CSをG19へ合わせ、
`m5stack-core2-usb-cs19` 環境を使用します。コードの環境選択だけではモジュールの物理経路は切り替わりません。

## 再利用できる制約・回避方法

### CoreS3 Liteのシリアルモニター

CoreS3 Liteでは、DTR/RTSをOFFにしたPlatformIOモニターを使用してください。
起動コマンドと終了方法は[導入方法](getting-started.md)を参照してください。

### 画面下の仮想ボタン

CoreS3の最下部20pxは左・中央・右がA/B/Cです。Nape操作と既存のタッチメニューも使用します。
