# ガチャメカ装備UI

仕様: https://app.notion.com/p/398b6c8875258093b8c2ca434b9c3d63

## 構成

- 左にSlot1〜4、右にCatalog順の候補（未解放もLOCKED表示で位置を維持）、下に名前・既存アイコン・役割・説明。
- 空にする操作あり。候補決定時に既存SetGadgetSlotで即時反映・保存。適用ボタンなし。同じ装備を選ぶと元のSlotから移動する既存処理を使用。
- 保存失敗を成功扱いせず、未保存の表示と同じ候補の再選択による保存再試行を追加。
- Pauseから開き、戻る操作はPauseへ戻す。サブ画面を閉じても時間停止を維持。
- 候補は既存GadgetCatalog順。新規解放の通知で更新。未実装・IDなし・重複IDを除外。未解放は装備・Test対象選択不可。
- 元のGameInstance／入力Context／GadgetComponentの責務を維持。表示名・役割・説明をGadgetDataAssetへ追加。
- アミの既存アイコンを再利用。新規素材を推測で固定しない。

## 確認方針

- 2026-09-27: Gadget Lab内のみ、装備／手動解放／Test対象選択の操作を追加。左列はLab時のみ52px行にして7行を384px内へ収める。Stage Pauseは4スロットの既存サイズを維持。
- 解放は累計Special Coin条件、消費なし、保存成功後に反映。Test対象は装備とは別のGameInstance一時状態。詳しくは `Gadget_Lab_Hub.md`。

- Development Editor C++ビルド成功。
- ユーザー指定によりPIE・UE実行テストは行わない。装備変更・保存再読込・Pause中の入力・使用中解除・Cooldown・解放更新の動作確認はユーザー担当。
- 2026-09-23：前回作成済みのLoadout関連Assetを保存。GadgetCatalogのBP_NetGadget、DA_GadgetのID=Net、PlayerControllerのLoadoutWidgetClassを再取得で確認。
- Project MapsのGameInstanceClassを作成済みBP_BoarGameInstanceに設定・保存・再取得。実行時の装備候補表示は未確認。
