# ガチャメカ装備UI

仕様: https://app.notion.com/p/398b6c8875258093b8c2ca434b9c3d63

## 構成

- 左にSlot1〜4、右にCatalog順の解放済み候補、下に名前・既存アイコン・役割・説明。
- 空にする操作あり。候補決定時に既存SetGadgetSlotで即時反映・保存。適用ボタンなし。同じ装備を選ぶと元のSlotから移動する既存処理を使用。
- 保存失敗を成功扱いせず、未保存の表示と同じ候補の再選択による保存再試行を追加。
- Pauseから開き、戻る操作はPauseへ戻す。サブ画面を閉じても時間停止を維持。
- 候補は既存GadgetCatalog順。新規解放の通知で更新。未解放・未実装・IDなし・重複IDを候補から除外。
- 元のGameInstance／入力Context／GadgetComponentの責務を維持。表示名・役割・説明をGadgetDataAssetへ追加。
- アミの既存アイコンを再利用。新規素材を推測で固定しない。

## 確認方針

- Development Editor C++ビルド成功。
- ユーザー指定によりPIE・UE実行テストは行わない。装備変更・保存再読込・Pause中の入力・使用中解除・Cooldown・解放更新の動作確認はユーザー担当。
- 2026-09-23：前回作成済みのLoadout関連Assetを保存。GadgetCatalogのBP_NetGadget、DA_GadgetのID=Net、PlayerControllerのLoadoutWidgetClassを再取得で確認。
- Project MapsのGameInstanceClassを作成済みBP_BoarGameInstanceに設定・保存・再取得。実行時の装備候補表示は未確認。
