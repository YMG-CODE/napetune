# 導入方法

## 1. プロジェクトを開く

VSCode + PlatformIOを用意します。

- **CoreS3 Lite**：取得したリポジトリの **prototype/** を開きます。
- **Core2用デモ版（G19）**：[リリースの対応ソース](firmware.md#対応ソースライセンス)を展開し、**core2-demo/** を開きます。

初回ビルド時に依存パッケージがダウンロードされます。
[必要な機材と配線](hardware.md)を確認してから書き込んでください。

## 2. 環境を選んで書き込む

配布バイナリを使う場合は[ファームウェアのダウンロード](firmware.md)を参照してください。

| 本体・構成 | 開くフォルダー | PlatformIO環境 |
|---|---|---|
| CoreS3 Lite | prototype/ | m5stack-cores3-lite-app |
| Core2用デモ版（G19） | 対応ソース内のcore2-demo/ | m5stack-core2-usb-cs19 |

VSCodeのProject Tasksで環境を選び、Build / Uploadを実行します。
ターミナルをprototype/で開いた場合のCoreS3例：

```sh
pio run -e m5stack-cores3-lite-app
pio run -e m5stack-cores3-lite-app -t upload --upload-port COM10
```

COM10は実際のポートに置き換えます。Core2では環境名も上表に合わせます。
リポジトリのルートから実行する場合は `pio run -d prototype ...` と指定します。
Core2用デモ版は`core2-demo/`内で、次を実行します。

```sh
pio run -e m5stack-core2-usb-cs19 -t upload --upload-port COM10
```

## 3. シリアルモニター

CoreS3では、DTR/RTSをOFFにしたPlatformIOモニターを使用します。

```sh
pio device monitor -p COM10 -b 115200 --dtr 0 --rts 0
```

終了はCtrl+Cです。
同梱の[PowerShell起動用スクリプト](../prototype/OPEN_CORES3_MONITOR_NO_RESET.ps1)も利用できます。

## 4. 操作と初回確認

ランチャーからTUNING、STORY、DEMO、LOUNGE、HANGARを選択します。
GLASS2には操作説明が表示されます（5秒ごとの2ページ切替）。
CoreS3の最下部20pxは仮想A/B/Cです。

起動後はタッチ/Nape入力、設定保存、ゲーム開始・終了、SD、GLASS2表示を確認します。
保存形式や設定は開発に伴い変更される場合があります。

## 環境が表示されない場合

ZIPやコピーを置き換えた後はProject Tasksを更新してください。
戻らない場合はVSCodeのReload Windowを使い、開いているフォルダーのplatformio.iniに対象環境があるか確認します。

## 従来の共通ソース構成（参考）

リポジトリの`prototype/`には、従来のCore2向けビルド環境も残っています。
現在配布するCore2用デモ版の再ビルドには、この共通ソースではなく、上記の`core2-demo/`を使用してください。

| 従来の環境 | USB CS | 補足 |
|---|---|---|
| m5stack-core2 | G33 | GLASS2のSCL G33と競合 |
| m5stack-core2-usb-cs19 | G19 | 共通ソースの従来構成。現行Core2配布版の対応ソースとは別 |

従来のG33構成でGLASS2を併用するには、USB CSの物理経路をG19へ合わせ、対応するビルド環境を使用します。
過去のビルド結果は[既存の記録](../prototype/BUILD_RESULTS.md)に保持しています。
