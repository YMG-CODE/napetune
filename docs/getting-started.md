# 導入方法

## 1. プロジェクトを開く

VSCode + PlatformIOを用意し、取得したリポジトリの **prototype/** を開きます。
初回ビルド時に依存パッケージがダウンロードされます。
[必要な機材と配線](hardware.md)を確認してから書き込んでください。

## 2. 環境を選んで書き込む

| 本体・構成 | PlatformIO環境 |
|---|---|
| CoreS3 Lite（既定） | m5stack-cores3-lite-app |
| Core2標準 | m5stack-core2 |
| Core2 USB CS19 | m5stack-core2-usb-cs19 |

VSCodeのProject Tasksで環境を選び、Build / Uploadを実行します。
ターミナルをprototype/で開いた場合のCoreS3例：

```sh
pio run -e m5stack-cores3-lite-app
pio run -e m5stack-cores3-lite-app -t upload --upload-port COM10
```

COM10は実際のポートに置き換えます。Core2では環境名も上表に合わせます。
リポジトリのルートから実行する場合は `pio run -d prototype ...` と指定します。

## 3. シリアルモニター

CoreS3では、DTR/RTSをOFFにしたPlatformIOモニターを使用します。

```sh
pio device monitor -p COM10 -b 115200 --dtr 0 --rts 0
```

終了はCtrl+Cです。
同梱の[PowerShell起動用スクリプト](../prototype/OPEN_CORES3_MONITOR_NO_RESET.ps1)も利用できます。

## 4. 操作と初回確認

ランチャーからTUNING、MECHA、DEMO、LOUNGE、HANGARを選択します。
公開版はGLASS2 v6ベースで、操作説明が表示されます（5秒ごとの2ページ切替）。
CoreS3の最下部20pxは仮想A/B/Cです。

起動後はタッチ/Nape入力、設定保存、ゲーム開始・終了、SD、GLASS2表示を確認します。
保存形式や設定は開発に伴い変更される場合があります。

## 環境が表示されない場合

ZIPやコピーを置き換えた後はProject Tasksを更新してください。
戻らない場合はVSCodeのReload Windowを使い、開いているフォルダーのplatformio.iniに対象環境があるか確認します。

## ビルドと実機確認の記録

[機種ごとの状態](hardware.md)と[既存ビルド結果](../prototype/BUILD_RESULTS.md)を区別してください。
今回の文書整理では、新しいビルド・実機試験を実施していません。
