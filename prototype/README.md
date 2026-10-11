# NapeTune — 『曐間のテルミナリス』体験版 / Prototype

© 2026 YMGWorks

M5Stack CoreS3 Lite / Core2とNapeProを使う、開発中の体験版です。
小説『曐間のテルミナリス』の進行に合わせて、物語・画像・ゲーム内容・仕様を追加・改変していく予定です。
正式版の仕様、互換性、収録内容を保証するものではありません。

**プログラムはオープンソース、作品素材は別の利用条件です。**
コードだけの商用利用もMITの範囲で可能ですが、物語・キャラクター素材の商用利用や転載は許可していません。
[適用範囲](LICENSE_SCOPE.md) / [MIT](LICENSE) / [作品素材の条件](CONTENT_LICENSE.md) /
[対象一覧](CONTENT_MANIFEST.json) / [第三者ライセンス](THIRD_PARTY_NOTICES.md)

## 収録内容

ランチャー、TUNING、MECHA、DEMO、LOUNGE、HANGAR。GLASS2には操作説明を5秒ごとに2ページで表示します。
フレアをM1/M2・F9〜F12に割当可能。従来のF8フレア操作も維持しています。
本公開版はGLASS2 v6のアプリソースを基に、不要な旧エンジンと開発資料を除外したソース公開版です。

## 必要な機材

- CoreS3 Lite（8MB QSPI PSRAM）またはCore2
- USB Module v1.2 / MAX3421E、およびNapeProの対応USBドングル
- GLASS2 Unit（任意）、microSD（保存・外部素材に使用）

USB ModuleのCS/INTセレクターと配線は使用ボードに合わせてください。Core2の配線をCoreS3へ流用しないでください。

| 信号 | CoreS3 Lite | Core2標準 |
|---|---|---|
| SPI SCK / MOSI / MISO | G36 / G37 / G35 | G18 / G23 / G38 |
| USB CS / INT | G7 / G10 | G33 / G35 |
| SD CS | G4 | G4 |
| GLASS2 SDA / SCL | G2 / G1 | G32 / G33 |

Core2でUSB CS G33とGLASS2 SCLを併用する構成にはピン競合があります。
GLASS2を併用する場合はUSB CSをG19に合わせ、`m5stack-core2-usb-cs19` 環境を使用してください。

## ビルドと書き込み

VSCode + PlatformIOで、このREADMEとplatformio.iniがあるフォルダーを開きます。
既定の環境は `m5stack-cores3-lite-app`。Core2用は `m5stack-core2` または `m5stack-core2-usb-cs19` です。
初回は依存パッケージがダウンロードされます。

```sh
pio run -e m5stack-cores3-lite-app
pio run -e m5stack-cores3-lite-app -t upload --upload-port COM10
pio device monitor -p COM10 -b 115200 --dtr 0 --rts 0
```

COM10は使用環境に合わせて変更してください。モニターはCtrl+Cで終了します。
Microsoft Serial Monitor拡張の開閉で停止する問題があるため、CoreS3では上記DTR/RTS OFFのPlatformIOモニターを使います。
再コピー後に環境一覧が更新されない場合はProject Tasksを更新するか、VSCodeをReload Windowしてください。

## 実機確認

ランチャー表示とタッチ/Nape入力、設定保存、ゲーム開始/終了、SD保存、GLASS2表示を確認してください。
CoreS3の最下部20pxは、左・中央・右が仮想A/B/Cです。
元のアプリ移植初版はユーザー実機で確認済みです。公開用コピーはビルド検証対象で、実機確認は別途必要です。
保存形式や設定は開発に伴って変更する場合があります。

## 作品素材とFork

個人的・非商用の体験、ローカルビルド/実行、GitHub規約上の閲覧/Forkを認めます。
素材の独立した転載、他作品への転用、素材を含む変更版の公開・商用配布等は別途許可が必要です。
プログラムロジックの権利とは分けて扱います。詳しくはCONTENT_LICENSE.mdを参照してください。
Forkや派生コードをYMGWorks公式・公認と誤認させる表示は行わないでください。

## 開発への参加

不具合報告・改善提案はIssue、コード変更はPull Requestで受け付けます。
投稿するコードは適用範囲に従いMITで提供してください。作品素材の追加・変更は事前に相談してください。
