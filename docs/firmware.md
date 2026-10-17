# ファームウェアのダウンロード

[GitHub Releasesの体験版](https://github.com/YMG-CODE/napetune/releases/tag/v0.1.0-prototype.1)から、使用する本体に合ったZIPをダウンロードします。

| ZIP | 対象・位置付け |
|---|---|
| Napetune-cores3-lite.zip | **CoreS3 Lite用。今後の主な開発対象**（8MB QSPI PSRAM） |
| Napetune-core2-demo.zip | **Core2用デモ版**。USB CSはG33 |
| Napetune-core2-demo-usb-cs19.zip | Core2用デモ版。GLASS2併用など、USB CSをG19に変更した構成向け |

Core2版はデモとして提供し、今後はCoreS3 Liteを主体に開発します。
この「Core2用デモ版」は提供方針を示す呼称です。アプリ内のSTORY・DEMO・HANGAR・LOUNGE・TUNINGは両機種に含まれます。
CoreS3向けのZIPは**CoreS3 Lite構成用**で、他のCoreS3製品での動作を保証するものではありません。

このリリースのバイナリは3構成でビルド確認済みです。配布バイナリの実機書き込み・操作確認は未実施です。
必要な機材、GPIOとUSBモジュールのスイッチ印字の違い、Core2標準構成のGLASS2との競合は[ハードウェア案内](hardware.md)を参照してください。

## 書き込み

ZIPを展開すると、書き込み用の統合イメージ（`*-merged.bin`）、手順、ライセンス通知が入っています。
書き込み前に必要な設定・データをバックアップし、シリアルモニターを閉じてください。
本体のファームウェアを書き換えます。Nape Pro自体のファームウェアは書き換えません。

Python環境で書き込みツールを用意します。

```sh
python -m pip install esptool==4.9.0
```

展開したフォルダーで、**使用する機種のコマンドだけ**を実行します。`COM10`は本体のポートに置き換えます。

CoreS3 Lite：

```sh
python -m esptool --chip esp32s3 --port COM10 --baud 460800 write_flash 0x0 Napetune-cores3-lite-merged.bin
```

Core2標準：

```sh
python -m esptool --chip esp32 --port COM10 --baud 460800 write_flash 0x0 Napetune-core2-demo-merged.bin
```

Core2 USB CS19構成：

```sh
python -m esptool --chip esp32 --port COM10 --baud 460800 write_flash 0x0 Napetune-core2-demo-usb-cs19-merged.bin
```

書き込み後は本体を再起動します。接続できない場合は本体の公式手順に従ってダウンロードモードにします。
書き込み後の操作・シリアルモニターは[導入方法](getting-started.md)を参照してください。

## 対応ソース・ライセンス

リリースにソースのコミットを記載し、`SHA256SUMS.txt`で各ZIPを識別します。
同じリリースの`Napetune-sources-and-relink.zip`には、対応するリポジトリ、使用したライブラリソース、Arduinoパッケージ、
オブジェクト・アーカイブと再リンク手順を収録します。Arduinoコアを変更して再リンクする際にも使用できます。

ソフトウェアと作品素材の条件は[既存の適用範囲](../LICENSE.md)、外部ライブラリは[第三者通知](../prototype/THIRD_PARTY_NOTICES.md)に従います。
バイナリの提供によって作品素材の利用条件を変更するものではありません。
