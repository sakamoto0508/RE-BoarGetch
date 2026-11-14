"""RetroRender スクリプト共通: プラグイン内パスと、プロジェクト固有設定の読み込み。

プロジェクト固有の値（パッチ対象マテリアル、スナップ用スカラーを足す追加 MPC など）はプラグインに書かず、
プロジェクト側の JSON に置く。既定の場所は <Project>/Config/RetroRender/RetroRenderProject.json で、
コマンドライン引数 -RetroConfig=<path> で差し替えられる。パッチ記録は同じフォルダの PatchRecord.json
（-RetroRecord=<path> で差し替え可）。設定ファイルが無い場合は空として扱い、レベルスキャンだけが使える。

RetroRenderProject.json の形式:
{
  "targets": {"/Game/.../M_Foo": ["snap", "texel", "world"], ...},   # パッチ対象と機能
  "snap_collections": ["/Game/.../MPC_Foo"],    # 既に MPC を 2 個使うマテリアル用（先頭が MF_RetroVertexSnap_LP の参照先）
  "chained": {"M_Foo": "MaterialExpressionAdd_5"} # CheckRetroMaterials.py: 元の WPO チェーンの検証
}
"""
from pathlib import Path
import json
import re
import sys
import unreal

# プラグインのコンテンツ（RetroRender.uplugin の CanContainContent）。共有アセットはすべてここに置く。
PLUGIN_ROOT = '/RetroRender'
# エンジン同梱の Interchange FBX Phong と、その複製（頂点スナップ等を足したもの）。
FBX_PHONG = '/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial'
FBX_COPY = PLUGIN_ROOT + '/Materials/M_RetroFBXPhong'


def scan_plugin_content():
    """コマンドレット実行ではプラグインのコンテンツがアセットレジストリに未登録のことがあり、
    does_asset_exist が False になる。使う前に /RetroRender を同期スキャンしておく。"""
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([PLUGIN_ROOT], True)


def command_args():
    """エディタのコマンドライン（バッチ実行）と sys.argv（"py <file> <args>" 実行）を連結したもの。"""
    return unreal.SystemLibrary.get_command_line() + ' ' + ' '.join(sys.argv[1:])


def _arg_value(name):
    m = re.search(r'-%s=(?:"([^"]+)"|(\S+))' % re.escape(name), command_args())
    return (m.group(1) or m.group(2)) if m else None


def project_dir():
    return Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


CONFIG_PATH = Path(_arg_value('RetroConfig') or project_dir() / 'Config' / 'RetroRender' / 'RetroRenderProject.json')
RECORD_PATH = Path(_arg_value('RetroRecord') or CONFIG_PATH.parent / 'PatchRecord.json')


def load_config():
    """{'targets': {path: set(features)}, 'snap_collections': [path], 'chained': {name: expression}}"""
    data = json.loads(CONFIG_PATH.read_text(encoding='utf-8')) if CONFIG_PATH.exists() else {}
    return {
        'targets': {path: set(features) for path, features in data.get('targets', {}).items()},
        'snap_collections': list(data.get('snap_collections', [])),
        'chained': dict(data.get('chained', {})),
    }


def load_record():
    return json.loads(RECORD_PATH.read_text(encoding='utf-8')) if RECORD_PATH.exists() else {}


def save_record(data):
    RECORD_PATH.parent.mkdir(parents=True, exist_ok=True)
    RECORD_PATH.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding='utf-8')
