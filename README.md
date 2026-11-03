# RE:BoarGetch

**Unreal Engine / C++主体で制作中の3D捕獲アクションゲーム**です。プレイヤーがガジェットを使ってイノシシを捕まえ、ステージの目標達成を目指します。

> **開発中のプロジェクト**です。ゲームプレイや演出、UIの調整を継続しています。

## 開発環境・実装方針

- **エンジン**：Unreal Engine `5.8`系（`.uproject`の`EngineAssociation`は`5.8`）
- **言語**：C++ / Blueprint
- **実装方針**：ゲームの状態・ルール・進行を主にC++、演出やエディタ上の調整をBlueprintとデータアセットで扱う

## 主要システム

### 1. ステージ進行の制御

[BoarGameMode.cpp](Source/REBoarGetch/Private/Core/BoarGameMode.cpp)がステージの進行、捕獲成立、終了要求の扱いをまとめています。プレイヤーのクリア・ゲームオーバー要求が同時期に発生した場合など、終了条件の競合に対する優先順位も設けています。

### 2. 捕獲の確定と進行データの分離

捕獲演出を開始した時点と、捕獲を正式に成立させる時点を分けています。確定待ちの個体をGameMode側で管理することで、演出の中断や重複通知により同じ個体が複数回記録されないようにしています。

参照：[BoarGameMode.cpp](Source/REBoarGetch/Private/Core/BoarGameMode.cpp)、[StageRunData.h](Source/REBoarGetch/Public/Stage/StageRunData.h)。

### 3. ガジェット管理のコンポーネント化

[GadgetComponent](Source/REBoarGetch/Public/Component/GadgetComponent.h)でガジェットの装備・使用・切替を管理しています。プレイヤーCharacterから装備管理の責務を分離し、最大4スロットの装備と、ロードアウトの保存に対応する構成です。

### 4. 継続データとステージ間の流れ

ロビーからステージへ入り、クリアまたはゲームオーバー後に再挑戦・ロビーへ戻る進行を構築しています。捕獲記録、ステージ達成状況、装備など、ラン中だけの状態と継続保存する状態を区別しています。

## 自動テストの実装例

- [StageProgressTests.cpp](Source/REBoarGetch/Private/Tests/StageProgressTests.cpp)：終了優先順位、入力ロック、挑戦中データ・セーブのシリアライズ、出現定義を検証
- [CaptureResolutionTests.cpp](Source/REBoarGetch/Private/Tests/CaptureResolutionTests.cpp)：捕獲個体の送致先選択と、檻の状態による選択の変化を検証

上記は**テストコードの実装箇所**です。このREADMEの作成時にUE Editor上で全テストの実行結果を確認したものではありません。

## プロジェクトを開く際の注意

1. Unreal Engine `5.8`系とC++ビルド環境を用意します。
2. `REBoarGetch.uproject`を開き、利用プラグインやプロジェクト依存アセットの有無を確認します。
3. `.uproject`には`StateTree`、`GameplayStateTree`、`ModelContextProtocol`、`RetroRender`などのプラグイン指定があります。環境によってはプラグインの導入・無効化・修復が必要です。
4. Editorで対象マップやBlueprintの参照を確認したうえでビルド・PIEを行ってください。

> 本リポジトリのソースだけで、第三者の環境での完全な再現を保証するものではありません。

## 今後の改善項目

- 捕獲時のカメラ・エフェクト・入力体験の調整
- UIや演出ロジックの責務の整理、エディタ上からの調整性改善
- 既存の自動テストとPIEによる回帰確認の充実

## この作品で伝えたいこと

Unreal Engine / C++で、操作・演出・ステージ進行・永続化までをつなぐゲームプレイ実装と、責務分離、状態の整合性、検証可能性を意識して開発しています。
