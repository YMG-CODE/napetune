# Napetune

**Napetuneは、M5StackとNapeProを組み合わせ、キーマップの表示・変更、ゲームやアプリの操作、対応ケース・筐体を開発するプロジェクトの総称です。**

© 2026 YMGWorks

## Napetuneとは

作者自身のTheExtendをベースに発展させている、開発中のプロジェクトです。現在は体験版・プロトタイプとして公開しています。
M5Stack向けファームウェア、ゲームやアプリケーション、オリジナル作品、対応するケース・筐体の開発を含みます。
小説の進行に合わせて内容・素材・仕様を追加・改変していく予定です。

## 主な機能・特徴

- **TUNING MODE**：NapeProのキーマップを表示・変更するモード。
- **GAMING MODE**：NapeProをコントローラーとして、ゲームやその他のアプリケーションを操作するモード。
- **『曐間のTERMINALIS』**：ゲームの世界観・設定をまとめたオリジナル作品。
- **対応ケース・筐体（開発中）**：NapeProと組み合わせて使え、M5Stackをマウントできる3Dプリントケース・筐体。

現在公開しているM5Stack向けファームウェアには、以下の機能があります。

- ランチャーからTUNING、MECHA、DEMO、LOUNGE、HANGARを選択。
- NapeProのUSBドングルとMAX3421Eモジュールによる入力、タッチ操作。
- GLASS2 Unitへの操作説明・メッセージ表示、microSDを用いた保存。
- Webで閲覧できる既存の体験ページと、M5Stackで動かすファームウェアを公開。

[Web体験ページ](https://ymg-code.github.io/napetune/) / [M5Stack版の説明・導入手順](prototype/README.md) /
[変更履歴](prototype/CHANGELOG.md)

## TheExtendとの関係

[TheExtend](https://github.com/YMG-CODE/M5stack_TheExtend)も、同じ作者であるYMGWorksのプロジェクトです。
M5Stack Core2と自作キーボード・PCをつなぐ拡張基板・アプリケーション群での開発を基に、Napetuneへ発展させています。
TheExtendを外部作者のプロジェクトとして扱うものではありません。
ただし、両プロジェクトが利用する外部ライブラリ・フォント等には、それぞれの第三者ライセンスが適用されます。

## 対応ハードウェア

- M5Stack CoreS3 Lite（QSPI PSRAM）またはCore2。
- USB Module v1.2 / MAX3421E、NapeProの対応USBドングル。
- GLASS2 Unit（任意）、microSD（保存・外部素材用）。

配線とビルド環境は[ハードウェア・ビルド手順](prototype/README.md)を参照してください。
モジュールのスイッチ印字と、本体のGPIO番号は区別して確認します。

## 『曐間のTERMINALIS』について

YMGWorksのオリジナル作品です。本プロジェクトにはストーリー、台詞、キャラクター・メカの画像や表現が含まれます。
これらの作品IP・素材は、プログラムに適用するMITライセンスの対象ではありません。
公開した作品素材の利用条件と対象は、[作品コンテンツ利用条件](prototype/CONTENT_LICENSE.md)と
[対象一覧](prototype/CONTENT_MANIFEST.json)で確認してください。

## ビルド・導入方法

リポジトリを取得し、VSCode + PlatformIOで **`prototype/` フォルダー**を開いてください。
既定環境は `m5stack-cores3-lite-app` です。
Core2用の環境、COMポート、モニターのDTR/RTS設定を含む既存の手順は
[prototype/README.md](prototype/README.md)にまとめています。

## 開発状況

ソース公開版はGLASS2 v6を基にしています。CoreS3 Lite・Core2通常版・Core2 CS19版について、
[2026-10-11のビルド記録](prototype/BUILD_RESULTS.md)があります。
今回の公開説明整備では動作コードを変更しておらず、新しいビルド・実機試験は実施していません。
開発用配布物のすべてが、このリポジトリへ反映済みとは限りません。
正式版の仕様・互換性・収録内容は未確定です。ファームウェアバイナリは同梱していません。

## 今後の展開

2027年3月ごろを目標に、Napetuneを組み込んで楽しめるキーボードやケース等の周辺機器の頒布を検討しています。
ソフトウェア自体の販売は予定していません。時期・内容は構想段階で、確定した頒布予定ではありません。

## ライセンス・権利について

**プログラムロジックはオープンソース、作品素材は別の利用条件です。**

- YMGWorks独自の対象コード：既存の[MITライセンス](prototype/LICENSE)。コードの改変・再配布・商用利用は、その条件に従えます。
- 物語・台詞・キャラクター・メカデザイン等：既存の[作品素材の利用条件](prototype/CONTENT_LICENSE.md)。コードの商用利用許可に、作品素材の商用利用許可は含まれません。
- 外部ライブラリ・フォント：[第三者ライセンス表示](prototype/THIRD_PARTY_NOTICES.md)。独自コードとは分けて扱います。
- 既存の `index.html` と埋め込み素材：今回のMITの対象外です。今回、そのファイルへの追加の利用許諾は行っていません。

[リポジトリ全体の適用範囲](LICENSE.md) / [コードと作品表現の区分](prototype/LICENSE_SCOPE.md)

このソフトウェアのMITを理由に、独自のキーボード・ケース・PCB・3Dモデル等の設計データの公開を求めるものではありません。
第三者設計を使う場合や、ファームウェア書込み済み製品を頒布する場合の別の条件は、
[公開監査・頒布時の確認事項](prototype/docs/PUBLIC_ANNOUNCEMENT_AUDIT.md)を参照してください。

## クレジット

開発・オリジナル作品：YMGWorks。開発元：[TheExtend](https://github.com/YMG-CODE/M5stack_TheExtend)。
M5StackのM5Unified / M5GFX / USB関連ライブラリ、TinyUSB / Adafruit、Arduino-ESP32、各フォントの作者に謝意を表します。
各権利者の著作権表示とライセンス文は[第三者ライセンス表示](prototype/THIRD_PARTY_NOTICES.md)から確認できます。
不具合・改善提案はIssueまたはPull Requestでお知らせください。
