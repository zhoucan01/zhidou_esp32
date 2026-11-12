"""Clear custom VS Code model configuration without reading/printing secret values."""
import argparse
import json
import sqlite3
from pathlib import Path


def targeted(key):
    return (key.startswith('secret://chat.lm.secret.') or
            key.startswith('chat.cachedLanguageModels') or
            key.startswith('chat.currentLanguageModel.'))


def run(root, apply):
    profiles = [root, *sorted((root / 'profiles').iterdir())]
    profiles = [p for p in profiles if (p / 'chatLanguageModels.json').is_file()]
    if len(profiles) != 8:
        raise RuntimeError(f'Expected 8 model configuration files; found {len(profiles)}')
    results = []
    for profile in profiles:
        config = profile / 'chatLanguageModels.json'
        data = json.loads(config.read_text(encoding='utf-8-sig'))
        if not isinstance(data, list):
            raise RuntimeError('Unexpected model configuration format')
        if apply:
            config.write_text('[]\n', encoding='utf-8')
        result = {'profile': profile.name, 'model_config_cleared': apply, 'storage': []}
        for name in ('state.vscdb', 'state.vscdb.backup'):
            database = profile / 'globalStorage' / name
            if not database.exists():
                continue
            connection = sqlite3.connect(database.as_uri() + ('?mode=rw' if apply else '?mode=ro'), uri=True, timeout=10)
            try:
                keys = [r[0] for r in connection.execute('SELECT key FROM ItemTable') if targeted(r[0])]
                if apply and keys:
                    connection.execute('PRAGMA secure_delete=ON')
                    with connection:
                        connection.executemany('DELETE FROM ItemTable WHERE key=?', [(key,) for key in keys])
                remaining = [r[0] for r in connection.execute('SELECT key FROM ItemTable') if targeted(r[0])]
                if apply and remaining:
                    raise RuntimeError('Model records remained after cleanup')
                result['storage'].append({'file': name, 'matched_records': len(keys), 'remaining': len(remaining)})
            finally:
                connection.close()
        if apply and json.loads(config.read_text()) != []:
            raise RuntimeError('Model configuration did not clear')
        results.append(result)
    print(json.dumps({'applied': apply, 'profiles': results}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    run(args.root, args.apply)
