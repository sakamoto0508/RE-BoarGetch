"""Compare reflected Blueprint APIs/settings and serialized data against audited main."""
from pathlib import Path
import re
import subprocess
import argparse

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', default='184aa6e13cb0f153b33434b8fe4b74959f43bacf')
parser.add_argument('--stage-select', action='store_true')
args = parser.parse_args()
baseline = args.baseline
files = [
    'Public/UI/BoarLoadoutWidget.h', 'Private/Core/BoarGameInstance.h',
    'Public/Core/BoarGameMode.h', 'Public/Player/BoarPlayerController.h',
    'Public/Component/GadgetComponent.h',
]
if args.stage_select:
    files += ['Public/UI/BoarLobbyWidget.h', 'Public/Stage/StageEntrance.h']

def declarations(text, macro):
    result = {}
    for match in re.finditer(r'\b' + macro + r'\(', text):
        pos, depth = match.end(), 1
        while depth:
            depth += (text[pos] == '(') - (text[pos] == ')')
            pos += 1
        metadata = text[match.end():pos - 1]
        end = re.search(r'[;{]', text[pos:])
        declaration = re.sub(r'\s+', ' ', text[pos:pos + end.start()]).strip()
        if macro == 'UPROPERTY' and 'Transient' in metadata:
            continue
        if macro == 'UFUNCTION':
            name = re.search(r'(\w+)\s*\(', declaration).group(1)
        else:
            name = re.search(r'(\w+)\s*(?:=.*)?$', declaration).group(1)
        result[name] = (re.sub(r'\s+', '', metadata), re.sub(r'\s+', '', declaration))
    return result

def old_text(file):
    return subprocess.check_output(
        ['git', 'show', f'{baseline}:Source/REBoarGetch/{file}'], cwd=root
    ).decode('utf-8-sig').replace('\r\n', '\n')

for file in files:
    current = (root / file).read_text(encoding='utf-8-sig')
    for macro in ('UFUNCTION', 'UPROPERTY'):
        old, new = declarations(old_text(file), macro), declarations(current, macro)
        for name, declaration in old.items():
            assert new.get(name) == declaration, f'{file}: changed {macro} {name}'
    print(f'Blueprint API/settings preserved: {file}')

for file in ('Private/Save/BoarSaveGame.h', 'Private/Save/BoarSaveGame.cpp',
             'Public/Stage/StageRunData.h', 'Public/Stage/StageConfig.h', 'Private/Stage/StageConfig.cpp'):
    assert (root / file).read_text(encoding='utf-8-sig') == old_text(file), f'Changed data contract: {file}'
    print(f'Data contract source unchanged: {file}')
