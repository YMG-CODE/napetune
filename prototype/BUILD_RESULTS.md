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

## DEMOモード削除後 — 2026-10-17

| Environment | Result |
|---|---|
| m5stack-cores3-lite-app | SUCCESS |
| m5stack-core2 | SUCCESS |
| m5stack-core2-usb-cs19 | SUCCESS |

依存バージョンは上記と同一。3構成合計115.323秒、コンパイラーのerror/warning表示なし。
DEMO専用の起動経路の削除とメニュー変更後のビルド確認です。実機確認は未実施です。
