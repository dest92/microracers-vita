import importlib.util
from pathlib import Path
import tempfile
import unittest
import ftplib
import struct
import threading
import time
import zipfile

spec = importlib.util.spec_from_file_location('deploy', Path(__file__).resolve().parents[1] / 'tools/vita_deploy.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


def make_sfo():
    key = b'TITLE_ID\0TITLE\0'
    title = b'Hot Wheels\0'
    key_off = 20 + 2 * 16
    value_off = key_off + len(key)
    title_id = b'HWMR00002\0'
    entries = struct.pack('<HHIII', 0, 0x0204, len(title_id), len(title_id), 0)
    entries += struct.pack('<HHIII', 9, 0x0204, len(title), len(title), len(title_id))
    return struct.pack('<5I', 0x46535000, 0x101, key_off, value_off, 2) + entries + key + title_id + title


class FakeFTP:
    def __init__(self, short=False):
        self.files = {deploy.APP + '/eboot.bin': b'old'}
        self.directories = {'/', 'ux0:', 'ux0:/', 'ux0:/app', deploy.APP,
                            'ux0:/data', deploy.DATA}
        self.writes = []
        self.short = short
    def voidcmd(self, cmd): pass
    def size(self, path):
        if path not in self.files: raise ftplib.error_perm('550 missing')
        return len(self.files[path])
    def pwd(self): return '/'
    def cwd(self, path):
        if path not in self.directories: raise ftplib.error_perm('550 missing')
    def storbinary(self, cmd, source, blocksize):
        self.writes.append(cmd[5:])
        self.files[cmd[5:]] = source.read()
        if self.short: self.files[cmd[5:]] = b''
    def rename(self, source, target):
        if target in self.files: raise ftplib.error_perm('550 exists')
        self.files[target] = self.files.pop(source)
    def delete(self, path): del self.files[path]
    def mkd(self, path): self.directories.add(path)
    def retrbinary(self, cmd, callback, blocksize=8192):
        path = cmd[5:]
        if path not in self.files: raise ftplib.error_perm('550 missing')
        callback(self.files[path])


class DeployTests(unittest.TestCase):
    def test_data_only_uploads_vpk_and_stage_without_install_or_launch(self):
        ftp = FakeFTP()
        commands = []
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vpk = root / 'release.vpk'
            vpk_size = None
            stage = root / 'stage'
            (stage / 'DATA').mkdir(parents=True)
            (stage / 'HotWheels.exe').write_bytes(b'exe')
            (stage / 'DATA' / 'track.dat').write_bytes(b'track')
            with zipfile.ZipFile(vpk, 'w') as archive:
                archive.writestr('sce_sys/param.sfo', make_sfo())
                archive.writestr('eboot.bin', b'eboot')
            vpk_size = vpk.stat().st_size
            runner = deploy.Deploy(ftp, lambda command: commands.append(command))
            runner.run(vpk, stage, data_only=True)
        self.assertEqual(commands, [])
        self.assertTrue(all(path.startswith(deploy.DATA + '/') for path in ftp.writes))
        self.assertEqual(ftp.size(deploy.DATA + '/hotwheels-vita.vpk'), vpk_size)
        self.assertEqual(ftp.size(deploy.DATA + '/HotWheels.exe'), 3)
        self.assertEqual(ftp.size(deploy.DATA + '/DATA/track.dat'), 5)

    def test_music_tracks_are_uploaded_to_the_path_the_resolver_finds(self):
        # El juego abre "MUSIC\TrackN.wav" y el resolver del port prueba primero
        # game_root/MUSIC/... (en la Vita: ux0:data/hotwheels/MUSIC/). Sin esto la
        # consola reproduce efectos pero la música MCI queda en silencio.
        ftp = FakeFTP()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vpk = root / 'release.vpk'
            with zipfile.ZipFile(vpk, 'w') as archive:
                archive.writestr('sce_sys/param.sfo', make_sfo())
                archive.writestr('eboot.bin', b'eboot')
            stage = root / 'stage'
            (stage / 'DATA').mkdir(parents=True)
            (stage / 'HotWheels.exe').write_bytes(b'exe')
            music = root / 'music'
            music.mkdir()
            (music / 'Track3.wav').write_bytes(b'track3')
            (music / 'nota.txt').write_bytes(b'x')
            deploy.Deploy(ftp, None).run(vpk, stage, data_only=True, music=music)
        self.assertEqual(ftp.size(deploy.DATA + '/MUSIC/Track3.wav'), 6)
        self.assertNotIn(deploy.DATA + '/MUSIC/nota.txt', ftp.files)

    def test_data_only_without_music_does_not_fail(self):
        ftp = FakeFTP()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vpk = root / 'release.vpk'
            with zipfile.ZipFile(vpk, 'w') as archive:
                archive.writestr('sce_sys/param.sfo', make_sfo())
                archive.writestr('eboot.bin', b'eboot')
            stage = root / 'stage'
            (stage / 'DATA').mkdir(parents=True)
            (stage / 'HotWheels.exe').write_bytes(b'exe')
            deploy.Deploy(ftp, None).run(vpk, stage, data_only=True)
        self.assertFalse(any('/MUSIC/' in path for path in ftp.files))

    def test_size_failure_preserves_original(self):
        ftp = FakeFTP(short=True)
        self.upload(ftp, expect_error=True)
        self.assertEqual(ftp.files[deploy.APP + '/eboot.bin'], b'old')
    def test_size_query_error_cleans_temporary_and_preserves_original(self):
        class SizeTimeout(FakeFTP):
            def size(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_temp('451 SIZE timeout')
                return super().size(path)
        ftp = SizeTimeout()
        remote = deploy.DATA + '/luna-size-cleanup.bin'
        ftp.files[remote] = b'old'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'x.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(ftplib.error_temp, '451 SIZE timeout'):
                deploy.Deploy(ftp, None).put(local, remote, force=True)
        self.assertEqual(ftp.files[remote], b'old')
        self.assertNotIn(remote + '.new', ftp.files)
    def test_size_query_error_reports_cleanup_failure(self):
        class SizeTimeoutAndNoDelete(FakeFTP):
            def size(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_temp('451 SIZE timeout')
                return super().size(path)
            def delete(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_perm('550 cleanup denied')
                return super().delete(path)
        ftp = SizeTimeoutAndNoDelete()
        remote = deploy.DATA + '/luna-size-cleanup-error.bin'
        ftp.files[remote] = b'old'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'x.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, r'451 SIZE timeout.*550 cleanup denied.*luna-size-cleanup-error\.bin\.new'):
                deploy.Deploy(ftp, None).put(local, remote, force=True)
        self.assertEqual(ftp.files[remote], b'old')
        self.assertIn(remote + '.new', ftp.files)
    def test_size_550_attempts_temporary_cleanup(self):
        class SizeUnavailable(FakeFTP):
            def size(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_perm('550 SIZE unavailable')
                return super().size(path)
        ftp = SizeUnavailable()
        remote = deploy.DATA + '/luna-size-550-cleanup.bin'
        ftp.files[remote] = b'old'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'x.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, 'SIZE remoto no disponible'):
                deploy.Deploy(ftp, None).put(local, remote, force=True)
        self.assertEqual(ftp.files[remote], b'old')
        self.assertNotIn(remote + '.new', ftp.files)
    def test_size_550_and_delete_error_report_unconfirmed_cleanup(self):
        class SizeUnavailableAndNoDelete(FakeFTP):
            def size(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_perm('550 SIZE unavailable')
                return super().size(path)
            def delete(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_perm('550 delete denied')
                return super().delete(path)
        ftp = SizeUnavailableAndNoDelete()
        remote = deploy.DATA + '/luna-size-550-cleanup-error.bin'
        ftp.files[remote] = b'old'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'x.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, r'SIZE remoto no disponible.*delete denied.*luna-size-550-cleanup-error\.bin\.new'):
                deploy.Deploy(ftp, None).put(local, remote, force=True)
        self.assertEqual(ftp.files[remote], b'old')
        self.assertIn(remote + '.new', ftp.files)
    def test_verified_rename_replaces_same_size(self):
        ftp = FakeFTP()
        self.upload(ftp)
        self.assertEqual(ftp.files, {deploy.APP + '/eboot.bin': b'new'})
    def test_same_size_corruption_preserves_original(self):
        # Medido en consola 2026-10-03: subida tras reboot dejó eboot del mismo
        # tamaño con otro hash; la verificación por tamaño no lo detectaba.
        class Corrupt(FakeFTP):
            def storbinary(self, cmd, source, blocksize):
                super().storbinary(cmd, source, blocksize)
                data = self.files[cmd[5:]]
                self.files[cmd[5:]] = bytes([data[0] ^ 0xFF]) + data[1:]
        ftp = Corrupt()
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'eboot.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, 'hash'):
                deploy.Deploy(ftp, None, lambda s: None).put(local, deploy.APP + '/eboot.bin', force=True)
        self.assertEqual(ftp.files[deploy.APP + '/eboot.bin'], b'old')
        self.assertNotIn(deploy.APP + '/eboot.bin.new', ftp.files)
    def test_interrupted_hash_retr_cleans_temporary_and_preserves_original(self):
        class Interrupted(FakeFTP):
            def retrbinary(self, cmd, callback, blocksize=8192):
                if cmd.endswith('.new'):
                    callback(b'n')  # simula bytes recibidos antes del corte
                    raise ftplib.error_temp('426 transfer aborted')
                return super().retrbinary(cmd, callback, blocksize)
        ftp = Interrupted()
        remote = deploy.DATA + '/luna-retr-cleanup.bin'
        ftp.files[remote] = b'old'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'eboot.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(ftplib.error_temp, '426 transfer aborted'):
                deploy.Deploy(ftp, None).put(local, remote, force=True)
        self.assertEqual(ftp.files[remote], b'old')
        self.assertNotIn(remote + '.new', ftp.files)
    def test_interrupted_hash_retr_reports_cleanup_failure(self):
        class InterruptedAndNoDelete(FakeFTP):
            def retrbinary(self, cmd, callback, blocksize=8192):
                if cmd.endswith('.new'):
                    callback(b'n')
                    raise ftplib.error_temp('426 transfer aborted')
                return super().retrbinary(cmd, callback, blocksize)
            def delete(self, path):
                if path.endswith('.new'):
                    raise ftplib.error_perm('550 delete denied')
                return super().delete(path)
        ftp = InterruptedAndNoDelete()
        remote = deploy.DATA + '/luna-retr-cleanup-error.bin'
        ftp.files[remote] = b'old'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'eboot.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, r'426 transfer aborted.*550 delete denied.*luna-retr-cleanup-error\.bin\.new'):
                deploy.Deploy(ftp, None).put(local, remote, force=True)
        self.assertEqual(ftp.files[remote], b'old')
        self.assertIn(remote + '.new', ftp.files)
    def test_rejects_other_app(self):
        ftp = FakeFTP()
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'eboot.bin'; local.write_bytes(b'new')
            with self.assertRaises(ValueError):
                deploy.Deploy(ftp, None).put(local, 'ux0:/app/DTWO00001/eboot.bin')
        self.assertEqual(ftp.files, {deploy.APP + '/eboot.bin': b'old'})
    def test_preserves_existing_save(self):
        ftp = FakeFTP()
        remote = deploy.DATA + '/DATA/MENU/settings.dat'
        ftp.files[remote] = b'user settings'
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'settings.dat'; local.write_bytes(b'default')
            deploy.Deploy(ftp, None).put(local, remote, preserve=True)
        self.assertEqual(ftp.files[remote], b'user settings')
    def test_final_size_failure_restores_backup(self):
        class BadFinal(FakeFTP):
            def rename(self, source, target):
                super().rename(source, target)
                if source.endswith('.new'): self.bad = True
            def size(self, path):
                if getattr(self, 'bad', False) and path == deploy.APP + '/eboot.bin':
                    self.bad = False
                    return 1
                return super().size(path)
        ftp = BadFinal()
        self.upload(ftp, expect_error=True)
        self.assertEqual(ftp.files[deploy.APP + '/eboot.bin'], b'old')
    def test_final_size_failure_removes_new_destination(self):
        class BadFinal(FakeFTP):
            def rename(self, source, target):
                super().rename(source, target)
                if source.endswith('.new'): self.bad = True
            def size(self, path):
                if getattr(self, 'bad', False) and path == deploy.DATA + '/x.bin':
                    self.bad = False
                    return 1
                return super().size(path)
        ftp = BadFinal(); ftp.files.clear()
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'x.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, 'tamaño final incorrecto'):
                deploy.Deploy(ftp, None, lambda s: None).put(local, deploy.DATA + '/x.bin', force=True)
        self.assertNotIn(deploy.DATA + '/x.bin', ftp.files)
        self.assertNotIn(deploy.DATA + '/x.bin.new', ftp.files)
    def test_failed_rollback_is_reported(self):
        class BadRollback(FakeFTP):
            def rename(self, source, target):
                super().rename(source, target)
                if source.endswith('.new'): self.bad = True
            def size(self, path):
                if getattr(self, 'bad', False) and path == deploy.DATA + '/x.bin':
                    self.bad = False
                    return 1
                return super().size(path)
            def delete(self, path):
                if path == deploy.DATA + '/x.bin': raise ftplib.error_perm('550 delete denied')
                super().delete(path)
        ftp = BadRollback(); ftp.files.clear()
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'x.bin'; local.write_bytes(b'new')
            with self.assertRaisesRegex(RuntimeError, 'rollback falló'):
                deploy.Deploy(ftp, None, lambda s: None).put(local, deploy.DATA + '/x.bin', force=True)
        self.assertEqual(ftp.files[deploy.DATA + '/x.bin'], b'new')
    def test_same_destination_deploys_are_serialized(self):
        entered = threading.Event(); release = threading.Event()
        class BlockingFTP(FakeFTP):
            def __init__(self):
                super().__init__(); self.stor_calls = 0
            def storbinary(self, cmd, source, blocksize):
                self.stor_calls += 1
                if not entered.is_set():
                    entered.set()
                    if not release.wait(2): raise RuntimeError('test gate timeout')
                super().storbinary(cmd, source, blocksize)
        ftp = BlockingFTP(); ftp.files.clear()
        errors = []
        with tempfile.TemporaryDirectory() as directory:
            one = Path(directory) / 'one'; one.write_bytes(b'one')
            two = Path(directory) / 'two'; two.write_bytes(b'two-two')
            target = deploy.DATA + '/x.bin'
            def run(path):
                try: deploy.Deploy(ftp, None, lambda s: None).put(path, target, force=True)
                except Exception as exc: errors.append(exc)
            first = threading.Thread(target=run, args=(one,)); first.start()
            self.assertTrue(entered.wait(1))
            second = threading.Thread(target=run, args=(two,)); second.start()
            time.sleep(0.05)
            self.assertEqual(ftp.stor_calls, 1, 'segundo STOR bloqueado por lock de destino')
            release.set(); first.join(2); second.join(2)
        self.assertFalse(first.is_alive() or second.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(ftp.files[deploy.DATA + '/x.bin'], b'two-two')
        self.assertNotIn(deploy.DATA + '/x.bin.new', ftp.files)
    # Spec 022 / BUG-026: sólo una sonda fresca con "main" prueba arranque.
    def launch_case(self, probe_after, survives=True):
        """probe_after: 'first' | 'reboot' | None — cuándo escribe main la app.
        survives: la app sigue viva pasados 15 s (False = suspendida, consola bloqueada)."""
        class ProbeFTP(FakeFTP):
            def delete(self, path):
                if path not in self.files: raise ftplib.error_perm('550 missing')
                del self.files[path]
            def retrbinary(self, cmd, callback):
                path = cmd[5:]
                if path not in self.files: raise ftplib.error_perm('550 missing')
                callback(self.files[path])
        ftp = ProbeFTP()
        ftp.files[deploy.DATA + '/hotwheels.log'] = b'[hw] log de la corrida anterior\n'
        ftp.files[deploy.PROBE] = b'ctor101 t=1\nmain t=2\n'  # sonda vieja
        commands = []
        def command(c):
            commands.append(c)
            launches = commands.count('launch ' + deploy.TITLEID)
            if c.startswith('launch') and (probe_after == 'first' or
                                           (probe_after == 'reboot' and 'reboot' in commands)):
                ftp.files[deploy.PROBE] = b'ctor101 t=9\nmain t=10\n'
                ftp.files[deploy.HB] = (b'hb start t=150\nhb t=5150 x\nhb t=20150 x\n' if survives
                                        else b'hb start t=150\nhb t=5150 x\nhb t=10150 x\n')
            if c == 'version': return 'vitacompanion 1.07'
            return 'Launched.' if c.startswith('launch') else 'ok'
        runner = deploy.Deploy(ftp, command, lambda s: None)
        deploy.CONSOLE_LOCKED = Path(tempfile.mkdtemp()) / 'CONSOLE_LOCKED'
        ok = runner.launch_verified(sleep=lambda s: None, polls=3, interval=1)
        return ok, commands

    def test_stale_log_is_not_life_and_triggers_reboot(self):
        ok, commands = self.launch_case(None)
        self.assertFalse(ok)
        self.assertEqual(commands.count('reboot'), 1)
        self.assertEqual(commands.count('launch ' + deploy.TITLEID), 2)

    def test_fresh_probe_on_first_launch_skips_reboot(self):
        ok, commands = self.launch_case('first')
        self.assertTrue(ok)
        self.assertNotIn('reboot', commands)

    def test_probe_after_reboot_marks_console_locked(self):
        # BUG-035: tras reboot la Vita queda en pantalla de bloqueo y suspende
        # la app a ~15 s; la sonda sola no prueba nada.
        ok, commands = self.launch_case('reboot', survives=False)
        self.assertFalse(ok)
        self.assertEqual(commands.count('reboot'), 1)
        self.assertLess(commands.index('quit all'), commands.index('reboot'))
        self.assertTrue(deploy.CONSOLE_LOCKED.exists())

    def test_app_suspended_at_15s_is_not_alive(self):
        ok, commands = self.launch_case('first', survives=False)
        self.assertFalse(ok)
        self.assertTrue(deploy.CONSOLE_LOCKED.exists())
        self.assertEqual(commands.count('reboot'), 0)  # rebootear la bloquearía de nuevo

    def upload(self, ftp, expect_error=False):
        with tempfile.TemporaryDirectory() as directory:
            local = Path(directory) / 'eboot.bin'; local.write_bytes(b'new')
            runner = deploy.Deploy(ftp, None, lambda s: None)
            if expect_error:
                with self.assertRaises(RuntimeError): runner.put(local, deploy.APP + '/eboot.bin', force=True)
            else:
                runner.put(local, deploy.APP + '/eboot.bin', force=True)


if __name__ == '__main__': unittest.main()
