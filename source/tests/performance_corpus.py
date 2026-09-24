"""Build an exact UTF-16 UI-text corpus for the C++ lookup benchmark."""
import json
from pathlib import Path
import struct


def main():
    root = Path(__file__).resolve().parents[2]
    translations = json.loads(
        (root / 'translations/dictionary_zh.json').read_text(encoding='utf-8-sig')
    )['translations']
    target = root / 'build/performance-corpus.bin'
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('wb') as output:
        output.write(struct.pack('<I', len(translations)))
        for text in translations:
            encoded = text.encode('utf-16-le')
            output.write(struct.pack('<I', len(encoded) // 2))
            output.write(encoded)
    print(f'{len(translations)} labels: {target}')


if __name__ == '__main__':
    main()
