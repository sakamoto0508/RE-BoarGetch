# 検証スクリプト（Tools/Verification）

RE:BoarGetchの回帰確認に使うスクリプトです。回帰テストの正本は`Source/REBoarGetch/Private/Tests`のC++ Automation Testで、ここにあるのはそれを実行・補完する道具です。

- どれも**手動で実行**します。ビルドやEditor起動時に自動では動きません。
- 実行結果（ログ・JSON・隔離したセーブ）はすべて`Saved/Verification/`へ出力します。`Saved/`はGit管理外です。
- 2026-11-02に`Source/REBoarGetch/Verification/`（ローカル管理）からGit履歴をもとに復元し、現在の構成に合わせて修正しました。**修正後はどのスクリプトも実行していないため、すべて動作未検証です。**

## 必要な環境

| 項目 | 内容 |
|---|---|
| OS / シェル | Windows。`.ps1`はWindows PowerShell 5.1以上、`.sh`はGit Bash（`cygpath`を使用） |
| Unreal Engine | `REBoarGetch.uproject`の`EngineAssociation`（現在5.8）。場所は`FindEngine.ps1`が解決 |
| Editorの状態 | 対象プロジェクトのEditorを閉じた状態で実行（アセットのファイルロックを避けるため） |
| ビルド | `REBoarGetchEditor`をビルド済みであること |
| Python（`VerifyPublicApi.py`のみ） | Python 3とGit。PATHの`python`がMicrosoft Storeのスタブの場合は、UE同梱の`Engine/Binaries/ThirdParty/Python3/Win64/python.exe`を使う |
| UE Plugin | Python Editor Script Plugin（`run_ue_*.sh`から実行するスクリプト） |

### エンジンの場所

`FindEngine.ps1`が次の順で探し、エンジンのルートを出力します。

1. 環境変数`UE_ENGINE_DIR`（例：`C:\Program Files\Epic Games\UE_5.8`）
2. Epic Launcherのインストール情報（`%ProgramData%\Epic\UnrealEngineLauncher\LauncherInstalled.dat`の`UE_<バージョン>`）
3. レジストリ（`HKLM\SOFTWARE\EpicGames\Unreal Engine\<バージョン>`、ソースビルドは`HKCU\Software\Epic Games\Unreal Engine\Builds`）

見つからない場合はErrorで終了します。そのときは`UE_ENGINE_DIR`を設定してください。

## スクリプト一覧

| ファイル | 目的 | 副作用 |
|---|---|---|
| `VerifyPhase.ps1` | `REBoarGetch.Spec`の全Automation Testを実行し、`BlueprintAllowList.txt`のBlueprintをコンパイル | Editorをヘッドレスで2回起動。アセット・セーブの保存なし。ログを出力 |
| `BlueprintAllowList.txt` | `VerifyPhase.ps1`がコンパイルするBlueprint一覧 | なし（データ） |
| `PieSmoke.py` | L_Stage01 / L_GadgetLab / L_GadgetTest_Net / L_ArchiveをPIEで開き、Pause・Loadout・端末・入力ロックを確認 | PIEとLevel読み込み。アセットは保存しない。セーブは`-UserDir`の隔離先へコピーして使う |
| `StageSelectPieSmoke.py` | L_Lobbyのステージ選択（カルーセル、ロック表示、Start遷移）をPIEで確認 | PIEとStage遷移。一時的なStageConfigはメモリ上だけ。セーブは`-UserDir`の隔離先だけに書く |
| `VerifyPublicApi.py` | Blueprintから見えるUPROPERTY/UFUNCTIONと、セーブ形式に関わるソースが比較元から変わっていないか確認 | 読み取りのみ（`git show`） |
| `run_ue_py.sh` | Pythonスクリプトをヘッドレス（NullRHI）のCommandletで実行 | 実行するスクリプト次第 |
| `run_ue_gui.sh` | Pythonスクリプトを画面外描画のEditorで実行（PIEが必要なスクリプト用） | 実行するスクリプト次第 |
| `FindEngine.ps1` | エンジンの場所を出力（上記） | なし |

## 実行例

プロジェクトのルートで実行します。

```powershell
# Automation Test一式とBlueprintコンパイル。-Phaseはログ名の区別に使う番号です。
powershell -ExecutionPolicy Bypass -File Tools\Verification\VerifyPhase.ps1 -Phase 1
```

```bash
# PIEスモーク。-UserDirは必須です（隔離されていなければ、PIEを始める前にErrorで止まります）。
Tools/Verification/run_ue_gui.sh Tools/Verification/PieSmoke.py -UserDir="$(cygpath -m "$PWD/Saved/Verification/PieSmokeUser")"
```

```bash
Tools/Verification/run_ue_gui.sh Tools/Verification/StageSelectPieSmoke.py -UserDir="$(cygpath -m "$PWD/Saved/Verification/StageSelectPieUser")"
```

```bash
# 比較元は必須です。PRの確認ではマージ先（origin/main）を指定します。
python Tools/Verification/VerifyPublicApi.py --baseline origin/main --stage-select
```

## 出力先

| 出力 | 場所 |
|---|---|
| `VerifyPhase.ps1`のログ | `Saved/Verification/Phase<N>-Tests.log`、`Phase<N>-Blueprint.log` |
| `run_ue_*.sh`のログ | `Saved/Verification/<スクリプト名>.log`、`<スクリプト名>_gui.log` |
| PIEスモークの結果 | `Saved/Verification/PieSmokeResults.json`、`StageSelectPieSmokeResults.json` |
| 隔離したセーブ | `Saved/Verification/PieSmokeUser/`、`StageSelectPieUser/` |

## 結果の見方

- **`VerifyPhase.ps1`**：最後まで進めば成功です。失敗時は例外で止まり、ログの場所を表示します。成功とみなす条件は次のとおりです。
  - 完了したテスト数が期待件数と一致し、すべて`Success`であること
  - 期待件数は`Private/Tests`の`IMPLEMENT_SIMPLE_AUTOMATION_TEST("REBoarGetch.Spec.…")`の数から自動で求めます。0件ならErrorにします。Complex TestやSpecが追加された場合は自動計算できないため、`-ExpectedTests`で指定するよう求めます。
  - AllowListの全アセットが存在し、AllowListの件数とコンパイル件数が一致し、エラー・警告が0件であること
  - Commandletが終了コード1でも、既知の`GameFeatureData`設定エラーだけが原因なら成功として扱います（以前から記録されている既存の問題です）。
- **PIEスモーク**：結果JSONの`error`が`null`、かつ`save_files_unchanged`（または`user_save_files_unchanged`）が`true`なら成功です。ログには`[RefactorPIE] COMPLETE`または`[StageSelectPIE] COMPLETE`が出ます。`-VerificationQuit`（`run_ue_gui.sh`が付けます）があれば、終了後にEditorを閉じます。
- **`VerifyPublicApi.py`**：変更を見つけると`AssertionError`で止まります。すべて一致すれば各ファイルの`preserved` / `unchanged`を出力します。

## 安全性

- 通常のセーブ（`Saved/SaveGames`）は読み取りだけで、削除も上書きもしません。PIEスモークは、通常のセーブのハッシュが実行前後で同じことを確認します。
- PIEスモークは`-UserDir`が隔離先を指していることを確認してから開始します。テスト用スロットの削除も隔離先でだけ行います。
- アセットの保存・削除・移動は行いません。`StageSelectPieSmoke.py`はEditorの`bThrottleCPUWhenNotForeground`を一時的にオフにし、終了時に戻します。
- `run_ue_py.sh`と`run_ue_gui.sh`は渡されたスクリプトをそのまま実行します。Asset変更を含むスクリプト（Plugin側の`SetupRetroAssets.py`など）を渡す場合は、そのスクリプトの副作用を確認してから実行してください。

## 現在の検証状況

| ファイル | 状況 |
|---|---|
| 全ファイル | **動作未検証**（2026-11-02の修正後は一度も実行していません。構文の静的確認のみ） |
| `PieSmoke.py` | 修正前の版は2026-10-02に4 Level・34項目で成功した記録があります（`Source/REBoarGetch/RefactorReport.md`）。修正後は、対象Levelの変更（`Stage1`→`L_Stage01`）と`-UserDir`による隔離を加えています。 |
| `StageSelectPieSmoke.py` | Widget名（`Text_StageStatus`など）は、2026-10-03時点のWBP構成を前提にしています。 |

## 旧パスとの対応

過去の作業報告（`RefactorReport.md`、`StageSelectRefactorReport.md`）にある`Verification/<ファイル名>`は、現在は`Tools/Verification/<ファイル名>`です。報告書の中のコマンドは当時の記録で、`-ExpectedTests 8`などの値も当時のものです。
