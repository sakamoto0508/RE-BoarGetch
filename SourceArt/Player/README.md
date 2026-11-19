# Player アニメーション原本と足音Notifyの復元

Playerの歩行・走行アニメーションは `Content/InportAssets/`（`.gitignore` で管理外）にある外部・インポートアセットを使う。
ライセンス・配布方針が未確認のため、アセット本体はGitに含めない。このフォルダでGit管理するのは、
自作した原本（`Animation/`）と、管理外アセットへ後から足す設定を復元するスクリプト（`Scripts/`）だけ。

| ファイル | 内容 |
|---|---|
| `Animation/A_Player_Walk_Net.*` `A_Player_Run_Net.*` | 網を持った歩き・走りの原本（.blend / .fbx / 調整値json）。UEの `/Game/InportAssets/Animation/` へImport |
| `Animation/Review/` | 比較用の画像 |
| `Scripts/restore_player_footstep_notifies.py` | 足音Notify（`AnimNotify_BoarFootstep`）を記録どおりに復元する |

## 外部Playerアセット（Gitに含まれない。別途導入が必要）

次のAssetを同じパスで配置する。入手元・ライセンスはNotion「インポートアセット一覧」でも未記載のため、再配布可否を確認するまでリポジトリへ追加しない（`git add -f` しない）。

| パス（/Game/InportAssets/…） | 用途 | 参照元 |
|---|---|---|
| `Player/GreatSwordSlash_UE`（Skeletal Mesh）・`_Skeleton`・`_PhysicsAsset` | Playerの見た目・Skeleton | BP_PlayerCharacter、全Playerアニメ |
| `Player/BS_Player_Ground`・`BS_Player_Ground_Net` | 地上移動のBlendSpace（Idle / Walk / Run） | ABP_Player |
| `Animation/A_Player_Idle`・`Walk`・`Run`・`Walk_Net`・`Run_Net` | 地上移動 | 上のBlendSpace |
| `Animation/A_Player_JumpStart`・`FallLoop`・`Land`・`DoubleJump` | ジャンプ | ABP_Player |
| `Player/IK_Player`、`Animation/UnityToUEAnim/` 等 | リターゲット | 制作時のみ |

Git管理側の依存：`/Game/Animation/Player/ABP_Player`、`/Game/BP/Player/BP_PlayerCharacter`、C++の `AnimNotify_BoarFootstep`（REBoarGetchモジュール）。

## 足音Notifyの復元

外部Playerアセットを導入した後、またはWalk / Runを再インポートした後に**手動で**実行する（Editor起動時などに自動実行する仕組みは無い）。

```bash
Tools/Verification/run_ue_py.sh "$PWD/SourceArt/Player/Scripts/restore_player_footstep_notifies.py"
```

- Git Bashでプロジェクトのルートから、Editorを閉じた状態で実行する。スクリプトは絶対パスで渡す（相対パスでは見つからない）。
- 変更せずに確認だけする場合は、先頭に `BOAR_FOOTSTEP_DRY_RUN=1` を付ける。
- Editorを開いている場合は、Output LogのCmd欄で `py "<スクリプトの絶対パス>"` を実行してもよい。
- 結果は `Saved/Verification/restore_player_footstep_notifies.json` とログの `[FootstepRestore]` に出る。

### 復元する内容（固定値。実行のたびに接地時刻を推定し直さない）

各Sequenceの `Footstep` トラックに `AnimNotify_BoarFootstep` を2個ずつ置く（Volume Multiplierは既定値の1.0）。

| Sequence | 長さ | 左足（FootBone=foot_l） | 右足（FootBone=foot_r） |
|---|---|---|---|
| A_Player_Walk | 1.3667秒 | 0.3758秒 | 1.0933秒 |
| A_Player_Run | 0.5333秒 | 0.1689秒 | 0.4356秒 |
| A_Player_Walk_Net | 1.3667秒 | 0.3758秒 | 1.0933秒 |
| A_Player_Run_Net | 0.5333秒 | 0.1689秒 | 0.4356秒 |

時刻は2026-11-19に、つま先（ball）と足首（foot）がどちらも最低点付近にある瞬間（振り出し中のつま先のかすりを除外）として求めた値。発音タイミングの最終判断はPIEで行う。

### 安全のための動作

- 先に4本すべてを検査し、1本でも次に当てはまれば**何も変更せずに停止**する：Assetが無い／AnimSequenceでない／Skeletonが `GreatSwordSlash_UE_Skeleton` でない／長さが記録と1%以上違う（再インポートで尺が変わった）／記録と違う `AnimNotify_BoarFootstep` がある（手動調整を上書きしない）。
- 同じClass・時刻（±0.002秒）・FootBoneのNotifyが既にあれば追加しない。何度実行しても各Sequenceは2個のまま。
- 追加するのは足音Notifyだけ。他のNotify・トラック（既存の `1` など）・Curve・Root Motion・Sequence設定・Skeletonは変更しない。不足が無ければ保存もしない。

### 復元できないもの

- 外部Playerアセット本体（Mesh・Skeleton・アニメーション・BlendSpace）。上の表のとおり別途導入する。
- 足音の音源。BP_PlayerCharacter の Player Audio > Footstep で設定する（未設定なら鳴らないだけでエラーにはならない）。
- 長さが変わったアニメーションの接地時刻。停止したら時刻を測り直し、スクリプトの `FOOTSTEPS` を更新する。

## BlendSpaceでの二重発火について

`BS_Player_Ground` / `BS_Player_Ground_Net` は Notify Trigger Mode が **Highest Weighted Animation**（2026-11-20確認）。Walk / Runの混合中も、最も重みの大きいサンプルのNotifyだけが発火するため、WalkとRunの足音が同時に重なることは無い。
ただし、重みの最大が入れ替わる瞬間は再生位置が別のアニメへ移るため、1歩抜ける・近い間隔で2回鳴る可能性はある（PIEで確認する）。

PIEでの確認：
1. BP_PlayerCharacter の Footstep に短いクリック音などを仮に設定する。
2. Stage 1 / 2 で、歩き → 走り → 歩きと速度をゆっくり変えながら移動し、境目で足音が重なったり抜けたりしないかを聞く。
3. 必要ならコンソールで `ShowDebug ANIMATION` を実行し、BlendSpaceのサンプル重みを見ながら境目を確認する。
4. 重なりが気になる場合は、BlendSpaceを変えずに、このスクリプトで置くNotifyの Trigger Weight Threshold を上げる対応を検討する（未実装）。
