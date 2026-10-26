#!/usr/bin/env python3
"""Construct *separate* experimental 8 GiB profile from immutable archived v0.9.3.

Does not invoke swapon, start a daemon, or touch a block device.
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import zipfile

MB = 1024 * 1024

PATCHES = {
    'nopen-vram-nbd.c': [
        ('EXPERIMENTAL 2 GiB swap canary.', 'EXPERIMENTAL 8 GiB swap canary.'),
        ('--mib 2048 [--swap-experiment', '--mib 8192 [--swap-experiment'),
        ('mb != 2048', 'mb != 8192'),
        ('with --mib 2048 (2 GiB)', 'with --mib 8192 (8 GiB)'),
        ('Nopen v0.9.3: 2048 MiB %s.', 'Nopen v0.9.3: 8192 MiB %s.'),
        # Both writes remain async-signal-safe; explicit results satisfy
        # Ubuntu/Debian _FORTIFY_SOURCE + -Werror=unused-result builds.
        ('(void)write(STDERR_FILENO,msg,n);',
         'ssize_t diagnostic_n = write(STDERR_FILENO,msg,n); (void)diagnostic_n;'),
        ('if(crash_log_fd>=0) (void)write(crash_log_fd,msg,n);',
         'if(crash_log_fd>=0) { diagnostic_n = write(crash_log_fd,msg,n); (void)diagnostic_n; }'),
        ('if (crash_log_fd >= 0) (void)write(crash_log_fd, msg, (size_t)n);',
         'if (crash_log_fd >= 0) { ssize_t event_n = write(crash_log_fd, msg, (size_t)n); (void)event_n; }'),
    ],
    'start-v093.sh': [
        ('The 2 GiB GPU reservation', 'The 8 GiB GPU reservation'),
        ('--mib 2048 --swap-experiment', '--mib 8192 --swap-experiment'),
        ('== 4194304 ]]', '== 16777216 ]]'),
    ],
    'enable-v093.sh': [
        ('!= 2147483648', '!= 8589934592'),
        ('NBD15 not 2 GiB', 'NBD15 not 8 GiB'),
        ('--mib 2048 --swap-experiment', '--mib 8192 --swap-experiment'),
    ],
    'test-real-swap-cycle.py': [
        ('if nbd["size_kib"] < 2 * 1024 * 1024 - 8192:',
         'if not (8 * 1024 * 1024 - 8192 <= nbd["size_kib"] <= 8 * 1024 * 1024):'),
        ('/dev/nbd15 is not a 2 GiB swap device', '/dev/nbd15 is not an 8 GiB swap device'),
    ],
    'focus-probe-v093.sh': [
        ('!= 4194304', '!= 16777216'),
        ('at 2 GiB.', 'at 8 GiB.'),
    ],
}


def apply_patches(folder):
    for filename, changes in PATCHES.items():
        file = folder / filename
        content = file.read_text(encoding='utf-8')
        for old, new in changes:
            actual = content.count(old)
            if actual != 1:
                raise RuntimeError(f'Fail-closed: expected exactly one {old!r} in {filename}, saw {actual}')
            content = content.replace(old, new, 1)
        file.write_text(content, encoding='utf-8')
    # Remove unrelated destructive RAW tests so only the explicitly guarded
    # 512 MiB anonymous-page canary is exposed in this profile.
    for name in ('test-nbd-2g.py', 'test-random4k-direct.py'):
        (folder / name).unlink(missing_ok=True)
    marker = folder / 'NOPEN_8G_PROFILE.txt'
    marker.write_text('Nopen isolated 8192 MiB capacity profile derived from v0.9.3.\n'
                      'Linux swap can still lose pages on NBD failure. Do not treat as stable.\n'
                      'Do not run any old 2GiB canary wrapper against this profile.\n',encoding='utf-8')
    # Structural invariants for safe size, memory and NBD access.
    code=(folder/'nopen-vram-nbd.c').read_text()
    assert 'mb != 8192' in code and 'size > globalmem * 3 / 4' in code
    assert 'pre_gpu_vram_free' in code and 'NBD_SET_TIMEOUT, 60ul' in code
    assert 'NBD_SET_SIZE_BLOCKS' in code
    assert 'NBD_SET_SOCK' in code
    en=(folder/'enable-v093.sh').read_text()
    assert '8589934592' in en and 'EVET SIGBUS RISKI' in en and 'swapon -p 200' in en
    start=(folder/'start-v093.sh').read_text()
    assert '16777216' in start and '--mib 8192' in start
    assert 'MemorySwapMax=0' in start and 'Restart=no' in start
    return folder


def extract_archive(archive,destination):
    if destination.exists():
        raise RuntimeError(f'Refusing overwrite: {destination}')
    with tempfile.TemporaryDirectory(prefix='nopen-8g-build-') as tmp:
        tmp=pathlib.Path(tmp)
        with zipfile.ZipFile(archive) as z:
            for member in z.infolist():
                path=pathlib.PurePosixPath(member.filename)
                if path.is_absolute() or '..' in path.parts or len(path.parts)<1:
                    raise RuntimeError('Unsafe archive member: '+member.filename)
            z.extractall(tmp)
        root=tmp/'Nopen_VRAM_NBD_v0.9.3'
        if not (root/'nopen-vram-nbd.c').is_file():
            raise RuntimeError('Archive missing expected v0.9.3 folder')
        apply_patches(root)
        shutil.copytree(root,destination,copy_function=shutil.copy2)
    print(f'CREATED 8 GiB profile: {destination}\nNO SWAP ENABLED; no kernel settings changed.')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--archive',type=pathlib.Path,required=True)
    ap.add_argument('--output',type=pathlib.Path,required=True)
    args=ap.parse_args()
    extract_archive(args.archive.expanduser().resolve(),args.output.expanduser().resolve())


if __name__=='__main__':
    main()