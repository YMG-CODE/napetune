# Build verification — 2026-10-11

| Environment | Result |
|---|---|
| m5stack-cores3-lite-app | SUCCESS |
| m5stack-core2 | SUCCESS |
| m5stack-core2-usb-cs19 | SUCCESS |

PlatformIO espressif32 6.12.0 / Arduino-ESP32 2.0.17 / M5Unified 0.2.25 / M5GFX 0.2.32。
3環境、合計93.628秒。コンパイラーのerror/warning表示なし。
PrBoomを除外した公開用コピーのビルド確認です。実機の動作確認を代替するものではありません。
生成したファームウェアは検証用で、ソース公開パッケージには含めていません。

## Core2 G19一本化後 — 2026-10-17

| Environment | Result |
|---|---|
| m5stack-core2-usb-cs19 | SUCCESS |
| m5stack-cores3-lite-app | SUCCESS（単一ジョブで再試行） |

選択可能な環境は上記2つです。共通設定の移動前後で有効なビルド設定が同じことを確認しました。
Core2のUSB CS既定値はG19、CoreS3 LiteはG7、GLASS2のSCLはそれぞれG33／G1であることをコンパイル時に確認。
実機確認は未実施です。

## ローカル操作・GLASS2設定合成の統合 — 2026-10-18

| Environment | Result | Libraries |
|---|---|---|
| m5stack-core2-usb-cs19 | SUCCESS | M5Unified 0.2.23 / M5GFX 0.2.30 |
| m5stack-cores3-lite-app | SUCCESS (`-j 1`) | M5Unified 0.2.25 / M5GFX 0.2.32 |

両機種とも`prototype/`の共通ソースからビルド。espressif32 6.12.0 / Arduino-ESP32 2.0.17。
Core2は99.948秒、CoreS3 Liteは115.503秒。コンパイラーのerror/warning表示なし。
ローカル/USBの独立した押下履歴、C選択とB決定の優先、長押し・会話終了時の入力消費、
F8〜F12の保存スロット維持、物理M1/M2/01〜04とゲーム設定の合成、前回マップと標準配置の切替を
ソース内の入力・表示ロジックをスタブ化して検証しました。実機のタッチ、USB再接続、SD読み書き、NVS保持は未確認です。
公開済みリリースのバイナリと対応ソースは変更していません。
