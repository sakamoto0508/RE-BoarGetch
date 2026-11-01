# DataPortal.usf

施設入口のData Portal（奥へ流れる遠近グリッド、まばらな発光ブロック、控えめなグリッチ）を描くHLSLの原本です。

## 形式

外部Shader Include（`#include`で読み込む`.usf`）ではありません。**MaterialのCustomノードへ貼り付ける本体コード**です。関数宣言を持たず、Customノードの入力名をそのまま変数として使い、最後に`return float4(色, マスク)`を返します（αは角丸矩形の開口マスク）。

使用している入力：`UV`、`Clock`、`ScrollSpeed`、`DistortionStrength`、`GridDensity`、`GridBrightness`、`DataBrightness`、`RimBrightness`、`NoiseStrength`、`CenterBlackness`、`EmissiveStrength`、`PortalColor`。名前を変える場合はCustomノード側の入力名も合わせます。

## 使用箇所（確認できた範囲）

- `/Game/Lobby/Materials/M_DataPortal_Tunnel`：本ファイルの全32行が、アセット内の文字列として存在することを確認しました（2026-11-02、ファイルの静的検索のみ。Editorでノードを開いての確認はしていません）。
- `/Game/Lobby/Materials/MI_DataPortal_Default`：上記のMaterial Instanceで、パラメータ名を含みます。

どのLevelのどのActorが使っているかは、今回は確認していません。

## 由来

`Source/REBoarGetch/Docs/DataPortal.usf`（origin/main、blob `807700a`）から内容を変えずに復元しました。Docsフォルダはローカル管理へ移行済みのため、ここをGit管理上の原本とします。

## 編集時の注意

- Materialは自動ではこのファイルを読みません。編集したら、`M_DataPortal_Tunnel`のCustomノードへ手作業で貼り直し、Materialを再コンパイルします。逆にMaterial側だけを直した場合は、このファイルも更新して内容をそろえます。
- 入力名を変えると、Material Instanceのパラメータ上書きが外れる場合があります。
- 奥行きはMaterialによる見かけの遠近表現です。実際のトンネル形状やカメラ視差はありません。
- `fwidth`を使うため、Pixel Shaderでのみ有効です。
