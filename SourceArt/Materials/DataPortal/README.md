# DataPortal.usf

施設入口のData Portal（奥へ流れる遠近グリッド、中心の奥行き発光、内側へ流れる弱いリング、まばらな発光ブロック、控えめなグリッチ）を描くHLSLの原本です。

## 形式

外部Shader Include（`#include`で読み込む`.usf`）ではありません。**MaterialのCustomノードへ貼り付ける本体コード**です。関数宣言を持たず、Customノードの入力名をそのまま変数として使い、最後に`return float4(色, マスク)`を返します（αは角丸矩形の開口マスク）。

使用している入力：`UV`、`Clock`、`ScrollSpeed`、`DistortionStrength`、`GridDensity`、`GridBrightness`、`DataBrightness`、`RimBrightness`、`NoiseStrength`、`CenterBlackness`、`EmissiveStrength`、`PortalColor`、`CamDist`、`CoreGlow`、`ApproachDistance`、`ApproachBoost`、`NearFadeStart`、`NearFadeEnd`。名前を変える場合はCustomノード側の入力名も合わせます。

`CamDist`はパラメータではなく、`Distance(CameraPositionWS, WorldPosition)`ノードの出力（cm）です。

## 近距離の挙動（AKI-142、2026-11-16）

- `CoreGlow`：開口の中心へ向かう奥行き発光。0にすると従来に近い「中心が暗い」見え方に戻ります。
- `ApproachDistance`→`NearFadeStart`：カメラが近づくほど、グリッド・リム・リングが`ApproachBoost`倍まで明るくなります。
- `NearFadeStart`→`NearFadeEnd`：カメラとの距離がこの範囲に入った画素から、粗いブロック単位で開口を消します（Masked用のDissolve。TAAに依存しない）。消える直前のブロックは少し光ります。カメラが面を越えた／面に近づきすぎたときに、面が画面を覆うのを防ぐためのものです。

## 使用箇所（2026-11-16にヘッドレスで確認）

- `/Game/Lobby/Materials/M_DataPortal_Tunnel`（Masked・Unlit・Two Sided）のCustomノード。
- `/Game/Lobby/Materials/MI_DataPortal_Default`：上記のMaterial Instance。`SM_DataPortal_Surface`（2三角形の板）が使います。
- `SM_DataPortal_Surface`の配置：`L_Lobby`（Stage / Gadget / Archive）、`L_GadgetLab`（Return / Test）、`L_GadgetTest_Net`（Return）、`L_Archive`（Return）、`L_GadgetLab_Redesign`（Return / Test）、`L_Archive_Redesign`（Return）。すべて同じMIを共有するため、親Materialの変更は全ポータルへ及びます。

## 由来

`Source/REBoarGetch/Docs/DataPortal.usf`（origin/main、blob `807700a`）から復元し、AKI-142で近距離フェードと奥行き発光を追加しました。Docsフォルダはローカル管理へ移行済みのため、ここをGit管理上の原本とします。

## 編集時の注意

- Materialは自動ではこのファイルを読みません。編集したら、`M_DataPortal_Tunnel`のCustomノードへ貼り直し、Materialを再コンパイルします。逆にMaterial側だけを直した場合は、このファイルも更新して内容をそろえます。
- 入力名を変えると、Material Instanceのパラメータ上書きが外れる場合があります。
- 奥行きはMaterialによる見かけの遠近表現です。実際のトンネル形状やカメラ視差はありません。
- `fwidth`を使うため、Pixel Shaderでのみ有効です。
