"""Parity of bb-prepare (tools/android/prepare/, C++) with tools/android/prepare_game.py.

Both programs run on the same input and must agree on the exit status, stdout, the last line of stderr
(Python prints a traceback, bb-prepare only its last line) and every output file byte for byte.
Inputs: the real game files (read only; skipped when absent), synthetic PS4 modules that go through the
whole pipeline (plain ELF and SELF wrappers, every relocation kind, the unit fixtures of test_prepare.py)
and seeded random corruptions of them.

Run: python3 -m unittest discover -s tests -p 'test_prepare_native.py'
Env: BB_TEST_GAME=<game folder> (default: game/CUSA03173 of this checkout or of the main one),
     BB_PREPARE=<bb-prepare binary> (default: out/host/bb-prepare, built when missing),
     BB_FUZZ_ITERATIONS (default 150), BB_FUZZ_SEED, BB_TEST_TMP (default: a temporary folder).
"""
from paths import ROOT
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from link_libc import encode_id
from prepare import nid
from test_prepare import fixture as unit_fixture

PY_TOOL = ROOT / 'tools/android/prepare_game.py'


def find_binary():
    env = os.environ.get('BB_PREPARE')
    if env:
        return Path(env)
    binary = ROOT / 'out/host/bb-prepare'
    if not binary.is_file():
        subprocess.run([str(ROOT / 'tools/android/build_prepare.sh'), 'host'], cwd=ROOT,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return binary


def find_game():
    candidates = []
    if os.environ.get('BB_TEST_GAME'):
        candidates.append(Path(os.environ['BB_TEST_GAME']))
    candidates.append(ROOT / 'game/CUSA03173')
    try:
        common = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--git-common-dir'], capture_output=True,
                                text=True).stdout.strip()
        candidates.append((ROOT / common).resolve().parent / 'game/CUSA03173')
    except OSError:
        pass
    return next((c for c in candidates if (c / 'eboot.bin').is_file()), None)


# ---- a minimal PS4-like module (plain ELF or SELF) -------------------------------------------------

FS_LOAD = bytes.fromhex('64488b042500000000')
LOAD_SIZE, LOAD_MEM = 0x2000, 0x3000
EXPORT, IMPORT = 0x12, 0x12


class Module:
    """Symbols are (name, info, shndx, value, size); relocs are (target, kind, symbol index, addend)."""

    def __init__(self, libs=None, mods=None, needed=(), tls=False, init=0, procparam=False):
        self.libs, self.mods = libs or {}, mods or {}
        self.needed, self.tls, self.init, self.procparam = list(needed), tls, init, procparam
        self.symbols, self.rela, self.plt = [('', 0, 0, 0, 0)], [], []
        self.code = bytearray(LOAD_SIZE)
        self.extra_ph = []
        self.spans = []  # byte ranges worth corrupting, in the plain ELF
        self.self_spans = []  # ... and in the SELF

    def build(self):
        strings = bytearray(b'\0')

        def s(name):
            if not name:
                return 0
            strings.extend(name.encode() + b'\0')
            return len(strings) - len(name.encode()) - 1
        dyn = []
        for tag, table in ((0x61000013, self.libs), (0x6100000d, self.mods)):
            for ident, (name, version) in table.items():
                dyn.append((tag, ident << 48 | version << 32 | s(name)))
        for name in self.needed:
            dyn.append((1, s(name)))
        sym = b''.join(struct.pack('<IBBHQQ', s(n), info, 0, shndx, value, size)
                       for n, info, shndx, value, size in self.symbols)
        pack = lambda r: struct.pack('<QQq', r[0], r[2] << 32 | r[1], r[3])
        rela, plt = b''.join(map(pack, self.rela)), b''.join(map(pack, self.plt))
        blob = bytes(strings)
        blob += bytes(-len(blob) % 8)
        str_size = len(strings)
        sym_off = len(blob)
        blob += sym
        rela_off = len(blob)
        blob += rela
        plt_off = len(blob)
        blob += plt
        dyn += [(0x61000035, 0), (0x61000037, str_size), (0x61000039, sym_off), (0x6100003f, len(sym)),
                (0x61000029, rela_off), (0x6100002d, len(rela)), (0x6100002f, plt_off), (0x61000031, len(plt))]
        if self.init:
            dyn.append((12, self.init))
        dyn.append((0, 0))
        dynamic = b''.join(struct.pack('<QQ', *d) for d in dyn)
        ph = [(1, 5, 0x1000, 0, LOAD_SIZE, LOAD_MEM, 0x1000)]
        if self.tls:
            ph.append((7, 4, 0x2800, 0x1800, 16, 32, 8))
        ph.append((2, 6, 0x3000, 0, len(dynamic), len(dynamic), 8))
        ph.append((0x61000000, 4, 0x4000, 0, len(blob), len(blob), 16))
        if self.procparam:
            ph.append((0x61000001, 4, 0x3800, 0x100, 0x40, 0x40, 8))
        ph += self.extra_ph
        header = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01\x09', 2, 62, 1, 0xa0, 64, 0, 0, 64, 56,
                             len(ph), 64, 0, 0)
        body = bytearray(0x4000 + len(blob))
        body[:len(header)] = header
        for i, (t, f, off, va, fsz, msz, al) in enumerate(ph):
            struct.pack_into('<IIQQQQQQ', body, 64 + 56 * i, t, f, off, va, va, fsz, msz, al)
        body[0x1000:0x1000 + LOAD_SIZE] = self.code
        body[0x3000:0x3000 + len(dynamic)] = dynamic
        body[0x3800:0x3840] = bytes(range(64))
        body[0x4000:] = blob
        self.ph_table = ph
        self.spans = [(0, 64 + 56 * len(ph)), (0x3000, 0x3000 + len(dynamic)), (0x4000, 0x4000 + len(blob))]
        return bytes(body)

    def build_self(self, uncovered=()):
        elf = self.build()
        ph = self.ph_table
        header_end = 64 + 56 * len(ph)
        covered = [i for i, p in enumerate(ph) if p[4] and i not in uncovered]
        base = 32 + 32 * len(covered)
        out = bytearray(base + header_end)
        out[:4] = b'O\x15=\x1d'
        struct.pack_into('<H', out, 24, len(covered))
        out[base:base + header_end] = elf[:header_end]
        self.self_spans = [(0, len(out))]
        for n, i in enumerate(covered):
            t, f, off, va, fsz, msz, al = ph[i]
            struct.pack_into('<QQQQ', out, 32 + 32 * n, 0x800 | i << 20, len(out), fsz, fsz)
            if t in (2, 0x61000000):
                self.self_spans.append((len(out), len(out) + fsz))
            out += elf[off:off + fsz]
        return bytes(out)


def sfo(entries):
    """A param.sfo; values are str (UTF-8), bytes (raw string) or int (format 0x404)."""
    keys = b''.join(k.encode() + b'\0' for k in entries)
    values, rows, key_off, data_off = b'', b'', 0, 0
    for k, v in entries.items():
        if isinstance(v, int):
            raw, fmt = struct.pack('<I', v), 0x404
        else:
            raw, fmt = (v.encode() if isinstance(v, str) else v) + b'\0', 0x204
        rows += struct.pack('<HHIII', key_off, fmt, len(raw), len(raw), data_off)
        key_off += len(k) + 1
        data_off += len(raw)
        values += raw
    key_table = 20 + 16 * len(entries)
    return struct.pack('<4sIIII', b'\0PSF', 0x101, key_table, key_table + len(keys), len(entries)) + rows + keys + values


def synthetic_game(extra_main=None, init_env=b'\xc3'):
    """An eboot, libc.prx and libSceFios2.prx that exercise every path of the linker."""
    libs = {1: ('libc', 1), 2: ('libkernel', 1), 3: ('libSceLibcInternal', 1)}
    mods = {0: ('libc', 1), 1: ('libkernel', 1)}
    A, B, C, D = (encode_id(i) for i in (0, 1, 2, 3))
    main = Module(libs, mods, needed=['libc.prx', 'libSceFios2.prx', 'libkernel.prx'], tls=True, procparam=True)
    main.code[0x10:0x19] = FS_LOAD
    main.code[0x100:0x109] = FS_LOAD
    main.code[0x1ff7:0x2000] = FS_LOAD
    main.code[0x200:0x209] = FS_LOAD
    main.code[0x209:0x212] = FS_LOAD  # back to back
    main.code[0x300:0x306] = FS_LOAD[:6]  # incomplete: not patched
    main.symbols += [
        ('AAAAAAAAAAA#B#A', IMPORT, 0, 0, 0),                 # 1: bound to libc's export
        ('BBBBBBBBBBB#B#A', 0x11, 0, 0, 0),                   # 2: object import nobody exports
        ('LLLLLLLLLLL#B#A', EXPORT, 1, 0x40, 8),              # 3: defined in the eboot
        ('', 0x00, 0xfff1, 0x1234, 0),                        # 4: absolute
        ('xinternal00#' + D + '#' + A, IMPORT, 0, 0, 0),      # 5: libSceLibcInternal -> libc's same NID
        ('AAAAAAAAAAA#C#B', IMPORT, 0, 0, 0),                 # 6: libkernel, nobody exports
        ('plain_name', IMPORT, 0, 0, 0),                      # 7: not a NID#lib#module name
        ('dataobject00#B#A', 0x11, 0, 0, 0),                  # 8: libc's object export, with an addend
    ]
    main.rela = [(0x100, 8, 0, 0x500), (0x108, 1, 1, 0), (0x110, 7, 2, 8), (0x118, 6, 3, 0x10), (0x120, 1, 4, 5),
                 (0x128, 1, 5, 0), (0x130, 1, 6, 0), (0x138, 1, 7, 0), (0x140, 1, 1, 0), (0x148, 1, 8, 16),
                 (0x150, 8, 0, 0x600)]
    main.plt = [(0x200, 7, 1, 0), (0x208, 7, 5, 0), (0x2f0, 1, 2, 4095)]
    if extra_main:
        extra_main(main)
    libc = Module({1: ('libc', 1), 2: ('libkernel', 1)}, {0: ('libc', 1), 1: ('libkernel', 1)}, tls=True, init=0x50)
    libc.code[0x300] = init_env[0]
    libc.code[0x10:0x19] = FS_LOAD
    libc.symbols += [
        ('', 0x03, 0, 0, 0),                                   # 1: TLS descriptor symbol
        ('AAAAAAAAAAA#B#A', EXPORT, 1, 0x100, 4),
        ('xinternal00#B#A', EXPORT, 1, 0x200, 4),
        (nid('_init_env') + '#B#A', EXPORT, 1, 0x300, 1),
        ('dataobject00#B#A', 0x11, 1, 0x1500, 8),
        ('module_start', 0x20, 0, 0, 0),                       # 6: weak undefined
        ('kkkkkkkkkkk#C#B', IMPORT, 0, 0, 0),                  # 7: libkernel import
        (nid('_ZNSt8ios_base4InitC1Ev') + '#B#A', EXPORT, 1, 0x400, 2),
        ('', 0x00, 0xfff1, 0xabcdef, 0),                       # 9: absolute
        ('localdata000#B#A', 0x11, 1, 0x1600, 8),              # 10
    ]
    libc.rela = [(0x1000, 8, 0, 0x700), (0x1008, 16, 1, 0), (0x1010, 6, 6, 0), (0x1018, 7, 7, 0), (0x1020, 1, 9, 3),
                 (0x1028, 6, 10, 8)]
    libc.plt = [(0x1030, 7, 7, 0)]
    fios = Module({1: ('libc', 1), 4: ('libNotInMain', 2)}, {0: ('libc', 1), 5: ('libNotInMain', 1)})
    fios.code[0x20:0x29] = FS_LOAD
    fios.symbols += [
        ('ffffffffff#B#A', EXPORT, 1, 0x80, 4),
        ('AAAAAAAAAAA#B#A', IMPORT, 0, 0, 0),
        ('newimport000#B#A', IMPORT, 0, 0, 0),
        ('zzzzzzzzzzz#' + encode_id(4) + '#' + encode_id(5), IMPORT, 0, 0, 0),
        ('gggggggggg#B#A', 0x11, 1, 0x88, 8),
    ]
    fios.rela = [(0x1000, 8, 0, 1), (0x1008, 1, 2, 0), (0x1010, 7, 3, 0), (0x1018, 7, 4, 0)]
    return main, libc, fios


def write_game(path, main, libc, fios, params=None, as_self=(True, True, True)):
    path = Path(path)
    (path / 'sce_module').mkdir(parents=True, exist_ok=True)
    (path / 'sce_sys').mkdir(exist_ok=True)
    for module, name, wrap in zip((main, libc, fios), ('eboot.bin', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx'),
                                  as_self):
        data = module if isinstance(module, bytes) else module.build_self() if wrap else module.build()
        (path / name).write_bytes(data)
    entries = {'TITLE': 'Test ™', 'TITLE_ID': 'CUSA03173', 'APP_VER': '01.09', 'USER_DEFINED_PARAM_1': 13}
    entries.update(params or {})
    (path / 'sce_sys/param.sfo').write_bytes(sfo(entries))
    return path


# ---- running both programs -----------------------------------------------------------------------

class Result:
    def __init__(self, proc, data):
        self.rc = proc.returncode
        data = str(data)
        real = os.path.realpath(data)
        clean = lambda t: t.replace(real, '<DATA>').replace(data, '<DATA>')
        self.stdout = clean(proc.stdout.decode('utf-8', 'surrogateescape'))
        lines = [l for l in proc.stderr.decode('utf-8', 'surrogateescape').splitlines() if l.strip()]
        self.stderr_last = clean(lines[-1]) if lines else ''
        self.files = {}
        root = Path(data)
        if root.is_dir():
            for p in sorted(root.rglob('*')):
                self.files[str(p.relative_to(root))] = None if p.is_dir() else p.read_bytes()


class NativeParity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = find_binary()
        if not cls.binary.is_file():
            raise unittest.SkipTest('no bb-prepare binary (tools/android/build_prepare.sh host)')
        base = os.environ.get('BB_TEST_TMP')
        cls.tmp = Path(base) if base else Path(tempfile.mkdtemp(prefix='bb-prepare-test-'))
        cls.tmp.mkdir(parents=True, exist_ok=True)
        cls.owns_tmp = not base
        cls.counter = 0

    @classmethod
    def tearDownClass(cls):
        if cls.owns_tmp:
            shutil.rmtree(cls.tmp, ignore_errors=True)

    def fresh(self, name):
        type(self).counter += 1
        path = self.tmp / f'{name}-{self.counter}'
        shutil.rmtree(path, ignore_errors=True)
        return path

    def run_py(self, game, data, env=None):
        proc = subprocess.run([sys.executable, str(PY_TOOL), str(game), str(data)], capture_output=True,
                              env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1', **(env or {})})
        return Result(proc, data)

    def run_native(self, game, data, env=None):
        proc = subprocess.run([str(self.binary), str(game), str(data)], capture_output=True,
                              env={**os.environ, **(env or {})})
        return Result(proc, data)

    def compare(self, game, env=None, name='case', py_data=None, native_data=None):
        py_data = py_data or self.fresh(name + '-py')
        native_data = native_data or self.fresh(name + '-native')
        py = self.run_py(game, py_data, env)
        native = self.run_native(game, native_data, env)
        self.assertParity(py, native)
        return py, native

    def assertParity(self, py, native):
        self.assertEqual(native.rc, py.rc, f'exit status\npython stderr: {py.stderr_last}\nnative: {native.stderr_last}')
        self.assertEqual(native.stdout, py.stdout)
        self.assertEqual(native.stderr_last, py.stderr_last)
        self.assertEqual(sorted(native.files), sorted(py.files))
        for name, data in py.files.items():
            if data is not None and native.files[name] != data:
                first = next((i for i, (a, b) in enumerate(zip(data, native.files[name])) if a != b),
                             min(len(data), len(native.files[name])))
                self.fail(f'{name} differs (sizes {len(data)} / {len(native.files[name])}, first difference at {first})')

    # -- the real game
    def test_real_game(self):
        game = find_game()
        if not game:
            self.skipTest('no game folder (BB_TEST_GAME)')
        py_data, native_data = self.fresh('real-py'), self.fresh('real-native')
        py, native = self.compare(game, py_data=py_data, native_data=native_data)
        self.assertEqual(py.rc, 0)
        self.assertIn('boot-linked.bin', py.files)
        self.assertIn('Game image: ready', native.stdout)
        # already prepared: both say so, and each accepts the other's image and marker
        for runner, data in ((self.run_py, native_data), (self.run_native, py_data)):
            again = runner(game, data)
            self.assertEqual(again.rc, 0)
            self.assertEqual(again.stdout, 'Game image: existing image matches selected game\n')
        self.assertParity(self.run_py(game, py_data), self.run_native(game, native_data))
        # another game folder (a symlink farm: same files, another path) invalidates the marker
        farm = self.fresh('real-farm')
        farm.mkdir()
        for name in ('eboot.bin', 'sce_module', 'sce_sys', 'dvdroot_ps4'):
            (farm / name).symlink_to(game / name)
        again_py = self.run_py(farm, py_data)
        self.assertIn('preparing selected game files', again_py.stdout)
        self.assertParity(again_py, self.run_native(farm, native_data))
        # a damaged marker and a missing image re-prepare
        for data in (py_data, native_data):
            (data / 'game-source.json').write_text('{not json')
        self.assertParity(self.run_py(game, py_data), self.run_native(game, native_data))
        for data in (py_data, native_data):
            (data / 'content.bin').unlink()
        self.assertParity(self.run_py(game, py_data), self.run_native(game, native_data))

    def test_real_game_shader_check(self):
        game = find_game()
        if not game:
            self.skipTest('no game folder (BB_TEST_GAME)')

        def dcx(payload, packed=None):
            packed = zlib.compress(payload) if packed is None else packed
            header = bytearray(0x4C)
            header[0:4], header[0x18:0x1C], header[0x24:0x28], header[0x28:0x2C] = b'DCX\0', b'DCS\0', b'DCP\0', b'DFLT'
            struct.pack_into('>II', header, 0x1C, len(payload), len(packed))
            return bytes(header) + packed
        good = dcx(b'shader' * 100)
        cases = {'ok': ({'a.dcx': good, 'sub/b.dcx': good, 'note.txt': b'x'}, True),
                 'no-folder': (None, True),
                 'wrong-size': ({'a.dcx': dcx(b'x' * 10, zlib.compress(b'y' * 11))}, False),
                 'bad-checksum': ({'sub/a.dcx': good[:-1] + bytes([good[-1] ^ 1])}, False),
                 'truncated-stream': ({'a.dcx': dcx(b'z' * 5000, zlib.compress(b'z' * 5000)[:10])}, False),
                 'other-compression': ({'a.dcx': good.replace(b'DFLT', b'KRAK')}, True),
                 'not-dcx': ({'a.dcx': b'hello'}, True),
                 'tiny': ({'a.dcx': b''}, True),
                 'trailing-garbage': ({'a.dcx': dcx(b'q' * 99, zlib.compress(b'q' * 99) + b'garbage')}, True),
                 'empty-stream': ({'a.dcx': dcx(b'', b'')}, False)}
        for name, (files, passes) in cases.items():
            with self.subTest(name):
                farm = self.fresh('shader-' + name)
                farm.mkdir()
                for entry in ('eboot.bin', 'sce_module', 'sce_sys'):
                    (farm / entry).symlink_to(game / entry)
                for rel, data in (files or {}).items():
                    target = farm / 'dvdroot_ps4/shader' / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                py, native = self.compare(farm, name='shader-' + name)
                self.assertEqual(py.rc == 0, passes, py.stderr_last)

    # -- synthetic modules
    def test_synthetic_game(self):
        main, libc, fios = synthetic_game()
        for combo in ((True, True, True), (False, False, False), (True, False, True), (False, True, False)):
            with self.subTest(wrapped=combo):
                game = write_game(self.fresh('synthetic-game'), main, libc, fios, as_self=combo)
                py, native = self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='synthetic')
                self.assertEqual(py.rc, 0, py.stderr_last)
                self.assertIn('Linked modules: libc.prx@', py.stdout)

    def test_synthetic_game_without_skip_is_rejected(self):
        main, libc, fios = synthetic_game()
        game = write_game(self.fresh('rejected-game'), main, libc, fios)
        py, native = self.compare(game, name='rejected')
        self.assertEqual(py.rc, 1)
        self.assertIn('GameCheckError', py.stderr_last)

    def test_init_env_not_ret(self):
        main, libc, fios = synthetic_game(init_env=b'\x90')
        game = write_game(self.fresh('init-env'), main, libc, fios)
        py, native = self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='init-env')
        self.assertIn('verified RET: False', py.stdout)

    def test_missing_metadata_headers_are_reported(self):
        main, libc, fios = synthetic_game(lambda m: m.extra_ph.append((0x6fffff01, 4, 0x3900, 0, 16, 16, 8)))
        game = write_game(self.fresh('missing-headers'), main, libc, fios)
        (game / 'eboot.bin').write_bytes(main.build_self(uncovered=(len(main.ph_table) - 1,)))
        py, native = self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='missing-headers')
        self.assertIn('Unavailable non-loadable metadata headers: [', py.stdout)

    def test_param_sfo_variants(self):
        main, libc, fios = synthetic_game()
        cases = {
            'invalid-utf8-title': {'TITLE': b'Bad \xff\xe2\x82 \xf0\x9f title \xc3\xa9\xed\xa0\x80end\xf4\x90'},
            'int-title': {'TITLE': 7},
            'no-title': {'TITLE': None},
            'params': {'USER_DEFINED_PARAM_2': 0xffffffff, 'USER_DEFINED_PARAM_4': 5},
            'string-param': {'USER_DEFINED_PARAM_3': 'x'},
            'empty-title': {'TITLE': ''},
        }
        for name, params in cases.items():
            with self.subTest(name):
                game = write_game(self.fresh('sfo-' + name), main, libc, fios)
                entries = {'TITLE': 'x', 'TITLE_ID': 'CUSA03173', 'APP_VER': '01.09', 'USER_DEFINED_PARAM_1': 13}
                entries.update(params)
                entries = {k: v for k, v in entries.items() if v is not None}
                (game / 'sce_sys/param.sfo').write_bytes(sfo(entries))
                self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='sfo-' + name)

    def test_param_sfo_damaged(self):
        main, libc, fios = synthetic_game()
        good = sfo({'TITLE': 'x', 'TITLE_ID': 'CUSA03173', 'APP_VER': '01.09'})
        cases = {'bad-magic': b'XXXX' + good[4:], 'truncated': good[:30], 'empty': b'',
                 'garbage-count': good[:16] + b'\xff' * 4 + good[20:],
                 'short-int': sfo({'TITLE': 'x', 'USER_DEFINED_PARAM_1': 1})[:-1],
                 'bad-key': good.replace(b'TITLE_ID', b'T\xffTLE_ID')}
        for name, data in cases.items():
            for skip in ('1', '0'):
                with self.subTest(name, skip=skip):
                    game = write_game(self.fresh('badsfo-' + name), main, libc, fios)
                    (game / 'sce_sys/param.sfo').write_bytes(data)
                    self.compare(game, env={'BB_SKIP_GAME_CHECK': skip}, name='badsfo-' + name)

    def test_unit_fixture_eboots(self):
        """The eboot fixtures of test_prepare.py (and their failing variants) through both programs."""
        base = bytes(unit_fixture())
        variants = {'fixture': base, 'truncated': base[:-1], 'empty': b'', 'tiny': b'O\x15'}
        for flag in (2, 8):
            data = bytearray(base)
            struct.pack_into('<Q', data, 32, 0x800 | flag)
            variants[f'encrypted-{flag}'] = bytes(data)
        data = bytearray(base)
        struct.pack_into('<Q', data, 32, 0)
        variants['missing-required'] = bytes(data)
        data = bytearray(base)
        struct.pack_into('<Q', data, 32, 0x100800)
        variants['bad-index'] = bytes(data)
        variants['not-self'] = b'\x00' * 64
        main, libc, fios = synthetic_game()
        for name, eboot in variants.items():
            for skip in ('1', '0'):
                with self.subTest(name, skip=skip):
                    game = write_game(self.fresh('unit'), eboot, libc, fios)
                    self.compare(game, env={'BB_SKIP_GAME_CHECK': skip}, name='unit-' + name)

    def test_game_check_messages(self):
        main, libc, fios = synthetic_game()
        cases = {'base-game': ('CUSA03173', '01.00'), 'wrong-eboot': ('CUSA03173', '01.09'),
                 'other-title': ('CUSA12345', '01.09'), 'no-param': None}
        for name, ident in cases.items():
            with self.subTest(name):
                game = write_game(self.fresh('check-' + name), main, libc, fios)
                (game / 'sce_sys/param.sfo').write_bytes(
                    sfo({'TITLE_ID': ident[0], 'APP_VER': ident[1]}) if ident else b'')
                py, native = self.compare(game, name='check-' + name)
                self.assertIn('GameCheckError', py.stderr_last)

    # -- errors around the folders
    def test_missing_files(self):
        main, libc, fios = synthetic_game()
        for victim in ('eboot.bin', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx', 'sce_sys/param.sfo',
                       'sce_module', 'sce_sys'):
            with self.subTest(victim):
                game = write_game(self.fresh('missing'), main, libc, fios)
                path = game / victim
                shutil.rmtree(path) if path.is_dir() else path.unlink()
                py, native = self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='missing')
                self.assertEqual(py.rc, 1)
        py, native = self.compare(self.tmp / 'does-not-exist', name='nogame')
        self.assertEqual(py.rc, 1)

    def test_usage(self):
        py = subprocess.run([sys.executable, str(PY_TOOL)], capture_output=True)
        native = subprocess.run([str(self.binary)], capture_output=True)
        self.assertEqual((native.returncode, py.returncode), (1, 1))
        self.assertIn(b'usage:', native.stderr)
        self.assertIn(b'GAME_DIR DATA_DIR', native.stderr)

    def test_data_dir_is_a_file(self):
        main, libc, fios = synthetic_game()
        game = write_game(self.fresh('datafile-game'), main, libc, fios)
        py_data, native_data = self.fresh('datafile-py'), self.fresh('datafile-native')
        py_data.write_text('x')
        native_data.write_text('x')
        py = self.run_py(game, py_data, {'BB_SKIP_GAME_CHECK': '1'})
        native = self.run_native(game, native_data, {'BB_SKIP_GAME_CHECK': '1'})
        self.assertEqual(py.rc, native.rc)
        self.assertEqual(py.stderr_last.split(':')[0], native.stderr_last.split(':')[0])

    def test_relative_paths_and_leftover_work_folder(self):
        main, libc, fios = synthetic_game()
        game = write_game(self.fresh('rel-game'), main, libc, fios)
        results = []
        for runner in ('py', 'native'):
            data = self.fresh('rel-' + runner)
            (data / 'prepare-tmp/deep').mkdir(parents=True)
            (data / 'prepare-tmp/deep/stale').write_text('old')
            cmd = ([sys.executable, str(PY_TOOL)] if runner == 'py' else [str(self.binary)]) + [
                os.path.relpath(game, self.tmp) + '//./', os.path.relpath(data, self.tmp) + '/']
            proc = subprocess.run(cmd, capture_output=True, cwd=self.tmp,
                                  env={**os.environ, 'BB_SKIP_GAME_CHECK': '1', 'PYTHONDONTWRITEBYTECODE': '1'})
            results.append(Result(proc, data))
        self.assertParity(*results)
        self.assertEqual(results[0].rc, 0)

    def test_stale_marker_variants(self):
        """A touched file, a changed module and a failing run that keeps the previous image."""
        main, libc, fios = synthetic_game()
        game = write_game(self.fresh('marker-game'), main, libc, fios)
        py_data, native_data = self.fresh('marker-py'), self.fresh('marker-native')
        env = {'BB_SKIP_GAME_CHECK': '1'}
        self.assertParity(self.run_py(game, py_data, env), self.run_native(game, native_data, env))
        again = self.run_native(game, native_data, env)
        self.assertEqual(again.stdout, 'Game image: existing image matches selected game\n')
        self.assertEqual(self.run_py(game, native_data, env).stdout, again.stdout)
        os.utime(game / 'sce_sys/param.sfo', ns=(1, 12345))
        self.assertParity(self.run_py(game, py_data, env), self.run_native(game, native_data, env))
        (game / 'sce_module/libc.prx').write_bytes((game / 'sce_module/libc.prx').read_bytes() + b'\0')
        self.assertParity(self.run_py(game, py_data, env), self.run_native(game, native_data, env))
        (game / 'eboot.bin').write_bytes(b'junk' * 100)
        self.assertParity(self.run_py(game, py_data, env), self.run_native(game, native_data, env))

    def test_non_ascii_game_path(self):
        main, libc, fios = synthetic_game()
        parent = self.fresh('unicode')
        game = write_game(parent / 'Gaéme ™ \U0001F3AE "quoted" \\ back', main, libc, fios)
        self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='unicode')
        odd = os.fsdecode(os.fsencode(str(parent)) + b'/bad\xff\xe2name')
        write_game(odd, main, libc, fios)
        self.compare(odd, env={'BB_SKIP_GAME_CHECK': '1'}, name='badbytes')

    # -- random corruptions of the synthetic files: same verdict, same message, same outputs
    def test_random_corruptions(self):
        iterations = int(os.environ.get('BB_FUZZ_ITERATIONS', '150'))
        rng = random.Random(int(os.environ.get('BB_FUZZ_SEED', '20261015')))
        modules = list(synthetic_game())
        outcomes = {}
        for n in range(iterations):
            files = []
            for module in modules:
                wrapped = rng.random() < 0.5
                data = bytearray(module.build_self() if wrapped else module.build())
                files.append((data, module.self_spans if wrapped else module.spans))
            for _ in range(rng.randint(1, 3)):
                data, spans = rng.choice(files)
                lo, hi = rng.choice(spans)
                pos = rng.randrange(lo, max(lo + 1, min(hi, len(data))))
                data[pos] = rng.choice([0, 1, 0xff, 0x80, data[pos] ^ (1 << rng.randrange(8)), (data[pos] + 1) & 255])
            game = write_game(self.fresh('fuzz-game'), *[bytes(d) for d, _ in files])
            with self.subTest(iteration=n):
                py, native = self.compare(game, env={'BB_SKIP_GAME_CHECK': '1'}, name='fuzz')
                key = py.stderr_last.split(':')[0] if py.rc else 'ok'
                outcomes[key] = outcomes.get(key, 0) + 1
        print(f'\nrandom corruptions: {iterations} runs, outcomes {dict(sorted(outcomes.items()))}', file=sys.stderr)


if __name__ == '__main__':
    unittest.main()
