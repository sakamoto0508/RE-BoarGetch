"""Validate a PIE smoke result JSON written by PieSmoke.py / StageSelectPieSmoke.py (read-only).

Usage: python check_smoke_result.py <ResultsJson> <RunId>
Exit 0 only when the file belongs to this run and reports a complete, error-free run with unchanged user saves.
"""
import json
import sys


def problems(path, run_id):
    try:
        with open(path, encoding='utf-8') as file:
            data = json.load(file)
    except FileNotFoundError:
        return ['result not written by this run: ' + path]
    except (OSError, ValueError) as error:
        return ['result could not be parsed: ' + str(error)]
    if not isinstance(data, dict):
        return ['result is not a JSON object']
    found = []
    if data.get('run_id') != run_id:
        found.append('run_id %r does not match this run %r (stale result?)' % (data.get('run_id'), run_id))
    if data.get('error') is not None:
        found.append('error: ' + str(data.get('error')).strip().splitlines()[-1])
    if data.get('complete') is not True:
        found.append('run did not reach its final step (complete is not true)')
    if data.get('user_save_files_unchanged') is not True:
        found.append('user save files changed or were not checked')
    passed = data.get('passed')
    required = data.get('required')
    if not isinstance(passed, list) or not isinstance(required, list) or not required:
        found.append('passed/required check lists are missing')
    else:
        missing = [label for label in required if label not in passed]
        if missing:
            found.append('required checks not passed: ' + ', '.join(missing))
    return found


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    found = problems(sys.argv[1], sys.argv[2])
    for problem in found:
        print('[SmokeResult] FAIL: ' + problem)
    if not found:
        print('[SmokeResult] PASS: ' + sys.argv[1])
    sys.exit(1 if found else 0)
