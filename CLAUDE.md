# atom_test — AtomLite試行錯誤用リポジトリ

AtomLite（TX/RX共通ファームウェア）の実装を試すための実験用リポジトリ。製品コードではない。

## 位置づけ

- 製品仕様・ハードウェア構成の正本は別リポジトリ `C:\workspace\MAKER\BT_SPEAKER`（`docs/hardware.md`・`docs/software.md`）
- ここでの試行錯誤の結果、製品に反映すべき知見（実装ノウハウ・仕様変更）が得られたら、BT_SPEAKERの `docs/impl-notes.md`・`docs/changes.md` 等に記録する
- 最終的な製品コードはBT_SPEAKERの `atom_a2dp/` に実装する（このリポジトリのコードをそのまま使うとは限らない）

## 作業ルール

- 推測で決めない。ライブラリAPI・ピンの挙動など不確かなものは、ローカル環境・データシート・ソースで確認するか、ユーザーに確認する
- 開発環境（arduino-cliのコア・ライブラリ）は BT_SPEAKER の `docs/dev-env.md` に準じる
