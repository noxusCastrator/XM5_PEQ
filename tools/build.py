"""Build only local artifacts from a caller-supplied official package and toolchain.

The temporary layout adapts the original guarded builders, preserving their offsets,
ABI settings and checks without depending on the development workspace.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, tarfile

REPO = Path(__file__).resolve().parents[1]
VERSION = json.loads((REPO / 'release.json').read_text())
BASE_SHA = '8b3f05155840c7b821f3bcbcd4690eaf64a00e867c7c3cef68f7be1959368be4'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def run(args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stock', type=Path, required=True)
    ap.add_argument('--toolchain', type=Path, required=True,
                    help='Root containing usr/bin/mipsel-linux-gnu-gcc-12')
    a = ap.parse_args()
    if sys.platform != 'linux' or os.geteuid() != 0:
        ap.error('Run as root in Linux/WSL; Linux rootfs ownership must be preserved.')
    stock, toolchain = a.stock.resolve(), a.toolchain.resolve()
    assert sha(stock) == BASE_SHA, 'Wrong official V2.43 package'
    assert (toolchain / 'usr/bin/mipsel-linux-gnu-gcc-12').is_file(), 'Missing pinned GCC12 toolchain'
    stage = REPO / 'build/work'
    assert not stage.exists(), 'build/work already exists; preserve outputs or remove it explicitly'
    stage.mkdir(parents=True)
    ws = stage / 'xm5-workspace'
    lab = ws / 'peq-lab'
    tasks = lab / 'tasks'
    package = ws / 'roundtrip-v2.43/unpacked/package'
    package.mkdir(parents=True)
    # Validate fixed tar names/types before extracting to our isolated staging tree.
    with tarfile.open(stock, 'r:') as archive:
        expected = {'firmware_v20.info', 'recovery-update', 'recovery-update/xImage',
                    'recovery-update/rootfs.squashfs'}
        assert {m.name.rstrip('/') for m in archive.getmembers()} == expected
        assert all(m.isfile() or m.isdir() for m in archive.getmembers())
        archive.extractall(package, filter='data')
    rootfs = ws / 'roundtrip-v2.43/unpacked/rootfs'
    run(['unsquashfs', '-no-progress', '-d', rootfs, package / 'recovery-update/rootfs.squashfs'])

    audio = tasks / 'A4-high-rate-peq'
    ui = tasks / 'U9-four-row-peq-ui'
    player = tasks / 'B6-high-rate-peq'
    pack = tasks / 'RX8-four-row-peq-package'
    shutil.copytree(REPO / 'src/audio', audio / 'src')
    shutil.copytree(REPO / 'src/ui', ui / 'src', ignore=shutil.ignore_patterns('vendor'))
    shutil.copytree(REPO / 'src/ui/vendor', ui / 'vendor')
    for origin, dest in [('tools/audio', player), ('tools/ui', ui), ('tools/package', pack)]:
        shutil.copytree(REPO / origin, dest / 'scripts')
        (dest / 'out').mkdir()
    (lab / 'ui-native-lab').mkdir(parents=True)
    shutil.copyfile(REPO / 'tools/awtk_resource.py', lab / 'ui-native-lab/export_stock.py')
    tr = tasks / 'T1-toolchain/toolchain/root'
    tr.parent.mkdir(parents=True)
    tr.symlink_to(toolchain, target_is_directory=True)

    run([sys.executable, player / 'scripts/derive_plt.py'])
    run(['bash', player / 'scripts/build_payload.sh'])
    run([sys.executable, player / 'scripts/build_candidate.py'])
    compact = player / 'out/hciplayer.compact'
    run([sys.executable, REPO / 'tools/elf_compact.py', '--input', player / 'out/hciplayer.candidate',
         '--output', compact, '--payload', player / 'out/payload.bin'])
    run([sys.executable, ui / 'scripts/resource.py'])
    run(['bash', ui / 'scripts/build.sh'])
    run([sys.executable, ui / 'scripts/patch_dual.py'])

    patch = json.loads((REPO / 'src/startup/patch.json').read_text())
    original_startup = rootfs / 'etc/init.d/S90play'
    assert sha(original_startup) == patch['stock_sha256']
    body = original_startup.read_bytes()
    for edit in sorted(patch['insertions'], key=lambda e: e['offset'], reverse=True):
        offset = edit['offset']
        body = body[:offset] + (REPO / edit['file']).read_bytes() + body[offset:]
    assert hashlib.sha256(body).hexdigest() == patch['output_sha256']
    overlay = pack / 'build/overlay'
    overlay.mkdir(parents=True)
    startup = overlay / 'S90play'
    startup.write_bytes(body)
    sources = {'release/bin/demo': ui / 'out/demo', 'usr/bin/hciplayer': compact,
               'etc/init.d/S90play': startup,
               'release/assets/default/raw/ui/playset/equalizer_page.bin': ui / 'out/equalizer_page.bin'}
    modes = [0o755, 0o775, 0o775, 0o744]
    lines = ['xm5-overlay-manifest\t1', 'model\tONIX XM5', 'base_version\tV2.43',
             'base_sha256\t' + BASE_SHA]
    for (rel, src), mode in zip(sources.items(), modes):
        assert sha(src) == VERSION['expected_overlays'][rel], 'Source build differs from 0.1.0 a: ' + rel
        dest = overlay / src.name
        if dest != src:
            shutil.copyfile(src, dest)
        lines.append('\t'.join(['file', rel, sha(rootfs / rel), sha(dest),
                                f'{mode:04o}', '0:0', 'overlay/' + dest.name]))
    manifest = pack / 'build/manifest.tsv'
    manifest.write_text('\n'.join(lines) + '\n')
    out = pack / 'out/alpha-built'
    run([sys.executable, pack / 'scripts/build_candidate.py', '--input', stock,
         '--overlay', manifest, '--out', out, '--tag', VERSION['firmware_tag']])
    image = out / 'update.bin'
    assert sha(image) == VERSION['update_sha256'], 'Rebuilt archive differs from the installed release'
    deliver = REPO / 'build/0.1.0-a'
    deliver.mkdir()
    shutil.copyfile(image, deliver / 'update.bin')
    shutil.copyfile(out / 'BUILD-MANIFEST.json', deliver / 'BUILD-MANIFEST.json')
    print('Built 0.1.0 a:', deliver / 'update.bin')

if __name__ == '__main__':
    main()
