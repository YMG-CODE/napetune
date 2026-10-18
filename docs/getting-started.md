# 導入方法

## 1. プロジェクトを開く

VSCode + PlatformIOを用意します。

CoreS3 Lite・Core2とも、リポジトリの **prototype/** を開きます。
公開済みリリースのバイナリを同じソースから再現したい場合は、[リリース別の対応ソース](firmware.md#対応ソースライセンス)を使用してください。

初回ビルド時に依存パッケージがダウンロードされます。
[必要な機材と配線](hardware.md)を確認してから書き込んでください。

## 2. 環境を選んで書き込む

配布バイナリを使う場合は[ファームウェアのダウンロード](firmware.md)を参照してください。

| 本体・構成 | 開くフォルダー | PlatformIO環境 |
|---|---|---|
| CoreS3 Lite | prototype/ | m5stack-cores3-lite-app |
| Core2用デモ版（G19） | prototype/ | m5stack-core2-usb-cs19 |

VSCodeのProject Tasksで環境を選び、Build / Uploadを実行します。
ターミナルをprototype/で開いた場合のCoreS3例：

```sh
pio run -e m5stack-cores3-lite-app
pio run -e m5stack-cores3-lite-app -t upload --upload-port COM10
```

COM10は実際のポートに置き換えます。Core2では環境名も上表に合わせます。
リポジトリのルートから実行する場合は `pio run -d prototype ...` と指定します。
Core2用デモ版も`prototype/`内で、次を実行します。

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
CoreS3の最下部20pxは、左から仮想A/B/Cです。Core2では本体のA/B/Cを使用します。

- A：ゲーム中は押している間、変形・ブースト（解禁後）。
- B／画面のNEXT：会話の決定・進行。通常戦闘中のBは攻撃です。
- C：会話の選択肢を循環。会話中のCは決定を兼ねません。

STORY/DEMOの自動操縦は既定ONです。既存OFF設定も更新後の初回だけONへ移行します。
以後、ゲームコンフィグでOFFにして保存すれば、その選択を維持します。
Nape Pro未接続でも、起動待機から5秒後にGLASS2初期化を試します。

ゲームコンフィグはKEYS／CONTROL／OPTIONSの3ページです。
KEYSはM1、M2、F8〜F12の順で、機能名ROLLはロール、BOOST TOGGLEは変形・ブーストの切替を示します。

GLASS2は、TUNING MODEで読み取った物理ボタンのキーマップとゲームコンフィグを組み合わせて、M1/M2/01〜04の操作を2ページで表示します。
例：TUNINGで03＝F8、ゲーム設定でF8＝FLAREなら「03 フレア射出」です。
未取得時は前回保存したキーマップを使い、前回値もなければ標準配置を使います。
本体キーマップを変更した場合はTUNING MODEで再読み込みし、ゲームに戻って数秒待つと前回値が保存されます。
異なるTAP/HOLDは「TAP/HOLD設定」、未対応の複合・透過キー等は「別キー設定」と表示します。

SDの準備・保存先・バックアップは[SDカードの使い方](sd-card.md)を参照してください。

起動後はタッチ/Nape入力、設定保存、ゲーム開始・終了、SD、GLASS2表示を確認します。
保存形式や設定は開発に伴い変更される場合があります。

## 環境が表示されない場合

ZIPやコピーを置き換えた後はProject Tasksを更新してください。
戻らない場合はVSCodeのReload Windowを使い、開いているフォルダーのplatformio.iniに対象環境があるか確認します。

## 公開済みリリースの再ビルド

現在の開発ソースは両機種共通です。公開済みv0.1.0-prototype.2は、それぞれのリリース時点のソース・ライブラリでビルドされています。
そのバイナリを再現する場合は[配布ファームウェアの対応ソース](firmware.md#対応ソースライセンス)を参照してください。
