# Contentフォルダ整理結果

状態: ユーザー承認後、12アセットを移動・保存済み。旧アセットと旧パス参照は12件すべて0件。BP配下のBlueprint以外のアセットも0件。

## 移動対象（12アセット）

パスはContentからの相対パス。移動はUnrealのAPIを利用する。

| 現在 | 移動先 |
| --- | --- |
| BP/Boar/A_Boar_Idle | Animation/Boar/A_Boar_Idle |
| BP/Boar/ABP_NormalBoar | Animation/Boar/ABP_NormalBoar |
| BP/Player/ABP_Player | Animation/Player/ABP_Player |
| BP/Boar/BoarDataAsset | DataAssets/Boar/DA_Boar_Default |
| BP/Boar/ST_Boar_Normal | AI/Boar/ST_Boar_Normal |
| BP/Cage/Materials/M_Cage_White | Art/Materials/Cage/M_Cage_White |
| BP/Cage/Materials/M_Cage_Navy | Art/Materials/Cage/M_Cage_Navy |
| BP/Cage/Materials/M_Cage_Yellow | Art/Materials/Cage/M_Cage_Yellow |
| BP/Cage/Materials/M_Cage_Glow | Art/Materials/Cage/M_Cage_Glow |
| Animaiton/Boar/BS_BoarLocomotion | Animation/Boar/BS_BoarLocomotion |
| Animaiton/Player/A_Player_Victory_NetRaise | Animation/Player/A_Player_Victory_NetRaise |
| Animaiton/Player/AM_Player_Victory_NetRaise | Animation/Player/AM_Player_Victory_NetRaise |

## 分類方針

- BP: 通常のBlueprint、Widget Blueprint。
- Animation: AnimSequence、AnimMontage、BlendSpace、AnimBlueprint。Boar/Playerで分ける。
- DataAssets: データアセット。BPに混在するBoarDataAssetは実際にBoarDataAsset型のインスタンスだった。既存のDA_BoarDataAssetとは別物なので統合せず保持する。
- AI: StateTree。
- Art/Materials: ゲーム用マテリアル。

購入素材やテンプレートの内部構造、レベル、その他の既存フォルダは今回の移動対象外。

## 実行・検証

1. 対象と再帰的な参照元の元ファイル、変更するソースをSaved/ContentOrganizationBackupへ保存。
2. Unrealのrename_assetで移動し、参照元の保存とリダイレクタ修復を行う。
3. CageVisualComponent.cpp、GadgetTestTarget.cppのマテリアル直書きパスを更新。移動パスを使うVerificationスクリプト、BlueprintAllowList、関連ドキュメントも更新。
4. C++ Editorターゲットをビルドし、Blueprintコンパイルと移動先のロード・旧参照の残存を検証する。

## 実施結果

- バックアップ: `C:/UnrealGames/REBoarGetch/Saved/ContentOrganizationBackup/20261021_224855`。対象および参照元54パッケージの元ファイルと変更前ソースを保存。
- 結果: `Verification/OrganizeContent.json`。移動先12件のロード成功、旧アセットなし、旧参照なし、BP内の型混在なし。
- プレイヤーとイノシシのAnimClassおよび勝利Montageが`/Game/Animation/`を参照することを確認。
- イノシシABPのBlendSpace参照を新配置へ修復して保存。
- 勝利Montage内部のAnimSequence参照も新配置へ修復して保存。空になった`Animaiton`と`BP/Cage/Materials`を除去。
- C++マテリアル直書きパス、検証スクリプト、BlueprintAllowList、関連資料を更新。
- 最終C++ Editorビルド成功。`Verification/ContentOrganizationBlueprints.log`で41 Blueprintのコンパイルエラー0、警告0、ロード失敗0を確認。
- 参照修復APIのUI依存により途中処理が失敗したため、参照元パッケージをロード・再保存し、対象のリダイレクタをUnreal APIで削除する手順で完了。一時的なC++修復APIは除去。
- プロジェクトに既存のGameFeatureData設定エラーがあるため、コマンドレットの終了コードだけで成功と判断せず、個別の結果・コンパイラ集計を確認する。
