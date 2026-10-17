# Napetune firmware prototype

CoreS3 Lite向けファームウェアのソースです。
TUNING、STORY、DEMO、LOUNGE、HANGARとGLASS2連携を含みます。

- [プロジェクト概要・画面](../README.md)
- [機種別ファームウェアのダウンロード](../docs/firmware.md)
- [導入方法](../docs/getting-started.md)
- [配線・対応機種の確認範囲](../docs/hardware.md)

VSCode + PlatformIOでこのフォルダーを開いてください。既定環境は `m5stack-cores3-lite-app` です。
書き込み用バイナリは[GitHub Releases](https://github.com/YMG-CODE/napetune/releases/tag/v0.1.0-prototype.2)で配布しています。

**Core2用デモ版（G19）の対応ソースは、リリース添付の`core2-demo/`です。**
このフォルダー内の従来のCore2環境とは別のため、配布版の再ビルドには`core2-demo/`を使用してください。
今後はCoreS3 Liteを主体に開発します。

## ライセンスと参加

独自の対象コードは[MIT](LICENSE)。物語・キャラクター等は[作品素材の利用条件](CONTENT_LICENSE.md)で扱い、MIT対象外です。
[適用範囲](LICENSE_SCOPE.md) / [素材一覧](CONTENT_MANIFEST.json) / [第三者ライセンス](THIRD_PARTY_NOTICES.md)

不具合・改善案はIssue、コード変更はPull Requestで受け付けます。
投稿するコードは適用範囲に従いMITで提供してください。作品素材の追加・変更は事前に相談してください。
公式・公認と誤認させる表示は行わないでください。
