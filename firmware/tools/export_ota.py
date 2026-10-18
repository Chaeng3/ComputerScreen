"""Export only a compatible application image, never a merged flash image."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def describe(data):
    if len(data) < 320 or len(data) > 0x400000:
        raise ValueError('Application size is outside OTA slot limits')
    if data[0] != 0xE9 or struct.unpack_from('<H', data, 12)[0] != 9 or data[23] != 1:
        raise ValueError('Expected an ESP32-S3 image with appended SHA-256')
    if struct.unpack_from('<I', data, 32)[0] != 0xABCD5432:
        raise ValueError('Expected an application descriptor at offset 32')
    version = data[48:80].split(b'\0', 1)[0].decode('ascii')
    project = data[80:112].split(b'\0', 1)[0].decode('ascii')
    if project != 'case_display' or not version.endswith('-ota'):
        raise ValueError('Expected a case_display release supporting OTA')
    if not all(c.isalnum() or c in '.-_' for c in version):
        raise ValueError('Unsafe version string')
    return {'project': project, 'version': version, 'chip': 'ESP32-S3',
            'board': 'case-display-s3-n16r8', 'partition_layout': 1,
            'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    data = (args.build_dir / 'case_display.bin').read_bytes()
    manifest = describe(data)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    name = f"case_display-{manifest['version']}.bin"
    (args.output_dir / name).write_bytes(data)
    manifest['file'] = name
    (args.output_dir / f"{name}.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"OTA file: {args.output_dir / name}")
    print(f"Version: {manifest['version']}; size: {manifest['size']}; SHA256: {manifest['sha256']}")


if __name__ == '__main__':
    main()
