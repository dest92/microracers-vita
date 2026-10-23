#!/usr/bin/env python3
"""Deploy FTP verificado para HWMR00002; data-only no instala ni lanza."""
import argparse
from contextlib import contextmanager
import ftplib
import fcntl
import hashlib
import io
import os
import re
from pathlib import Path
import socket
import struct
import tempfile
import time
import zipfile

TITLEID = 'HWMR00002'
APP = 'ux0:/app/' + TITLEID
DATA = 'ux0:/data/hotwheels'
# Spec 022: miga síncrona que sólo escribe un proceso recién creado (ctor101 + main).
PROBE = DATA + '/boot_probe.txt'
# Spec 023: el vigía escribe "hb t=<ms>" cada 5 s; sirve para probar que la app
# sigue viva pasado el período de gracia de ~15 s (BUG-035).
HB = DATA + '/hb.txt'
# BUG-035: tras un reboot la Vita queda en pantalla de bloqueo y el sistema
# suspende toda app propia a ~15 s del launch. Sólo un humano puede desbloquear;
# quien desbloquee borra este flag.
CONSOLE_LOCKED = Path(__file__).resolve().parents[1] / 'out/CONSOLE_LOCKED'
CONSOLE_DOWN = Path(__file__).resolve().parents[1] / 'out/CONSOLE_DOWN'


def sfo_strings(blob):
    magic, _, keys, values, count = struct.unpack_from('<5I', blob)
    if magic != 0x46535000:
        raise ValueError('param.sfo inválido')
    result = {}
    for i in range(count):
        keyoff, _, length, _, valueoff = struct.unpack_from('<HHIII', blob, 20 + i * 16)
        start = keys + keyoff
        key = blob[start:blob.index(b'\0', start)].decode()
        result[key] = blob[values + valueoff:values + valueoff + length].rstrip(b'\0').decode(errors='replace')
    return result


class Deploy:
    def __init__(self, ftp, command, report=print):
        self.ftp, self.command, self.report = ftp, command, report
        self.sent = self.skipped = 0

    def size(self, path):
        try:
            self.ftp.voidcmd('TYPE I')
            return self.ftp.size(path)
        except ftplib.error_perm as e:
            if not str(e).startswith('550'):
                raise
            return None

    def exists_dir(self, path):
        previous = self.ftp.pwd()
        try:
            self.ftp.cwd(path)
            return True
        except ftplib.error_perm as e:
            if not str(e).startswith('550'):
                raise
            return False
        finally:
            self.ftp.cwd(previous)

    def mkdirs(self, path):
        parts = path.split('/')
        for i in range(2, len(parts) + 1):
            directory = '/'.join(parts[:i])
            if not self.exists_dir(directory):
                self.ftp.mkd(directory)

    def fetch(self, path):
        out = io.BytesIO()
        self.ftp.retrbinary('RETR ' + path, out.write)
        return out.getvalue()

    @contextmanager
    def _destination_lock(self, remote):
        # Deploy runs on the Linux host. Keep the lock file (unlinking it while
        # another process waits can split flock users across two inodes).
        key = hashlib.sha256(remote.encode('utf-8')).hexdigest()
        path = Path(tempfile.gettempdir()) / ('hw-vita-deploy-' + key + '.lock')
        with path.open('a') as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)

    def put(self, local, remote, preserve=False, force=False):
        if CONSOLE_DOWN.exists():
            raise RuntimeError("CONSOLE_DOWN: deploy suspendido")
        if not (remote.startswith(APP + '/') or remote.startswith(DATA + '/')):
            raise ValueError('destino fuera del alcance autorizado')
        with self._destination_lock(remote):
            self._put_locked(local, remote, preserve, force)

    def _put_locked(self, local, remote, preserve, force):
        local_size = local.stat().st_size
        existing = self.size(remote)
        if existing is not None and (preserve or (not force and existing == local_size)):
            self.skipped += 1
            return
        self.mkdirs(remote.rsplit('/', 1)[0])
        temporary = remote + '.new'
        with local.open('rb') as source:
            self.ftp.storbinary('STOR ' + temporary, source, blocksize=256 * 1024)
        try:
            measured = self.size(temporary)
        except Exception as size_error:
            try:
                self.ftp.delete(temporary)
            except Exception as cleanup_error:
                raise RuntimeError(f'{temporary}: SIZE falló: {size_error}; '
                                   f'falló limpieza remota: {cleanup_error}; revisar {temporary}') from size_error
            raise
        if measured != local_size:
            mismatch = (f'{temporary}: SIZE remoto no disponible (None), local {local_size}'
                        if measured is None else
                        f'{temporary}: remoto {measured} != local {local_size}')
            try:
                self.ftp.delete(temporary)
            except Exception as cleanup_error:
                raise RuntimeError(f'{mismatch}; limpieza remota no confirmada: {cleanup_error}; '
                                   f'revisar {temporary}') from cleanup_error
            raise RuntimeError(f'{mismatch}; temporal eliminado; original intacto')
        # El tamaño no alcanza: una subida tras reboot dejó un eboot del mismo
        # tamaño con otro contenido (2026-10-03). Releer y comparar SHA-256
        # ANTES de tocar el original.
        local_hash = hashlib.sha256()
        with local.open('rb') as source:
            for block in iter(lambda: source.read(1 << 20), b''):
                local_hash.update(block)
        remote_hash = hashlib.sha256()
        try:
            self.ftp.retrbinary('RETR ' + temporary, remote_hash.update, blocksize=256 * 1024)
        except Exception as retr_error:
            try:
                self.ftp.delete(temporary)
            except Exception as cleanup_error:
                raise RuntimeError(f'{temporary}: RETR SHA-256 falló: {retr_error}; '
                                   f'falló limpieza remota: {cleanup_error}; revisar {temporary}') from retr_error
            raise
        if remote_hash.digest() != local_hash.digest():
            try:
                self.ftp.delete(temporary)
            except Exception as cleanup_error:
                raise RuntimeError(f'{temporary}: hash remoto distinto del local; '
                                   f'falló limpieza del temporal: {cleanup_error}')
            raise RuntimeError(f'{temporary}: hash remoto distinto del local; original intacto')
        # VitaShell FTP no garantiza RNTO sobre un destino existente. Conservá
        # el anterior dentro del mismo scope hasta completar el rename.
        backup = remote + '.previous'
        if existing is not None:
            if self.size(backup) is not None:
                self.ftp.delete(backup)
            self.ftp.rename(remote, backup)
        try:
            self.ftp.rename(temporary, remote)
            if self.size(remote) != local_size:
                raise RuntimeError('tamaño final incorrecto: ' + remote)
        except Exception as original_error:
            rollback_error = None
            try:
                if self.size(remote) is not None:
                    self.ftp.delete(remote)
                if existing is not None:
                    self.ftp.rename(backup, remote)
            except Exception as exc:
                rollback_error = exc
            if rollback_error is not None:
                raise RuntimeError(f'{original_error}; rollback falló: {rollback_error}; '
                                   f'remote={remote}, backup={backup if existing is not None else "<ninguno>"}') from original_error
            raise
        if existing is not None:
            self.ftp.delete(backup)
        self.sent += 1
        self.report(f'OK {remote}: {local_size} bytes, sha256 verificado')

    # La música MCI: el juego abre "MUSIC\TrackN.wav" y el resolver del port
    # prueba primero game_root/MUSIC/... — en la Vita, ux0:data/hotwheels/MUSIC/.
    # Sin esta subida la consola reproduce efectos pero la música queda en
    # silencio (medido en Vita3K: el emulador pedía ux0:data/hotwheels/MUSIC/).
    # Se sube a MUSIC/ y no a ux0:/music/ para no escribir en la raíz del
    # dispositivo del usuario.
    def _upload_music(self, music):
        if music is None or not Path(music).is_dir():
            self.report('música: no hay carpeta; la consola quedará SIN música MCI')
            return 0
        tracks = sorted(p for p in Path(music).glob('Track*.wav') if p.is_file())
        if not tracks:
            self.report(f'música: {music} no tiene Track*.wav; la consola quedará SIN música MCI')
            return 0
        for local in tracks:
            self.put(local, DATA + '/MUSIC/' + local.name, force=True)
        self.report(f'música: {len(tracks)} pistas en {DATA}/MUSIC/')
        return len(tracks)

    def run_data_only(self, vpk, stage, music=None):
        with tempfile.TemporaryDirectory(prefix='hw-deploy-') as temporary:
            root = Path(temporary)
            with zipfile.ZipFile(vpk) as archive:
                for name in archive.namelist():
                    if name.startswith('/') or '..' in Path(name).parts:
                        raise ValueError('ruta inválida en VPK')
                archive.extractall(root)
            expected = sfo_strings((root / 'sce_sys/param.sfo').read_bytes())
            if expected.get('TITLE_ID') != TITLEID:
                raise ValueError('VPK no es HWMR00002')
            if not (stage / 'HotWheels.exe').is_file():
                raise ValueError('faltan los datos stageados')
            self.put(vpk, DATA + '/hotwheels-vita.vpk', force=True)
            for local in sorted(stage.rglob('*')):
                if not local.is_file():
                    continue
                rel = local.relative_to(stage).as_posix()
                if rel.lower().endswith(('.log', '.new', '.previous', '.core')) or rel.lower() in ('progress.txt', 'core', 'boot_probe.txt'):
                    continue
                preserve = rel.lower() in ('data/menu/settings.dat', 'data/menu/highscore.dat')
                # An existing env.txt is user configuration; seed only if absent.
                self.put(local, DATA + '/' + rel, preserve=preserve or rel.lower() == 'env.txt')
            self._upload_music(music)
            self.report(f'data-only: {self.sent} subidos, {self.skipped} omitidos; sin instalar ni lanzar')
            return 0

    def _drop_probe(self):
        try:
            self.ftp.delete(PROBE)
        except ftplib.error_perm as e:
            if not str(e).startswith('550'):
                raise

    def _probe_main(self, sleep, polls, interval):
        for _ in range(polls):
            sleep(interval)
            if CONSOLE_DOWN.exists():
                raise RuntimeError('CONSOLE_DOWN durante launch')
            try:
                if b'main' in self.fetch(PROBE):
                    return True
            except ftplib.error_perm as e:
                if not str(e).startswith('550'):
                    raise
        return False

    def _hb_max_ms(self):
        try:
            text = self.fetch(HB).decode(errors='replace')
        except ftplib.error_perm as e:
            if not str(e).startswith('550'):
                raise
            return 0
        values = [int(m) for m in re.findall(r'^hb (?:start )?t=(\d+)', text, re.M)]
        return max(values, default=0)

    def _survives(self, sleep, need_ms=20000, polls=6, interval=5):
        for _ in range(polls):
            sleep(interval)
            if self._hb_max_ms() >= need_ms:
                return True
        return False

    def _locked(self, why):
        CONSOLE_LOCKED.parent.mkdir(parents=True, exist_ok=True)
        CONSOLE_LOCKED.write_text(why + '\n')
        self.report('CONSOLE_LOCKED: ' + why + ' — requiere desbloqueo humano; no rebootear (vuelve a bloquear)')
        return False

    def launch_verified(self, sleep=time.sleep, polls=6, interval=5, boot_wait=150):
        """BUG-026: `Launched.` no prueba un proceso nuevo (una instancia trabada
        sobrevive a `quit all` y launch la reanuda). Sólo cuenta la sonda fresca;
        si falta, un único reboot + relanzamiento."""
        if CONSOLE_LOCKED.exists():
            self.report('CONSOLE_LOCKED presente: consola bloqueada, no se lanza')
            return False
        self._drop_probe()
        self.report(self.command('launch ' + TITLEID))
        if self._probe_main(sleep, polls, interval):
            if self._survives(sleep):
                return True
            return self._locked('app viva al arrancar pero detenida antes de 20 s (pantalla de bloqueo)')
        self.report('launch sin sonda fresca: instancia previa trabada; quit all + reboot')
        self.command('quit all')
        self.command('reboot')
        sleep(20)
        for _ in range(max(1, boot_wait // 5)):
            try:
                if 'companion' in (self.command('version') or '').lower():
                    break
            except OSError:
                pass
            sleep(5)
        else:
            return False
        sleep(10)  # shell listo para lanzar tras el arranque
        self.command('nosleep on')
        self._drop_probe()
        self.report(self.command('launch ' + TITLEID))
        if not self._probe_main(sleep, polls, interval):
            return False
        if self._survives(sleep):
            return True
        return self._locked('reboot dejó la consola en pantalla de bloqueo')

    def run(self, vpk, stage, data_only=False, music=None):
        if data_only:
            return self.run_data_only(vpk, stage, music)
        with tempfile.TemporaryDirectory(prefix='hw-deploy-') as temporary:
            root = Path(temporary)
            with zipfile.ZipFile(vpk) as archive:
                for name in archive.namelist():
                    if name.startswith('/') or '..' in Path(name).parts:
                        raise ValueError('ruta inválida en VPK')
                archive.extractall(root)
            expected = sfo_strings((root / 'sce_sys/param.sfo').read_bytes())
            if expected.get('TITLE_ID') != TITLEID:
                raise ValueError('VPK no es HWMR00002')
            installed = self.exists_dir(APP)
            if installed:
                current = sfo_strings(self.fetch(APP + '/sce_sys/param.sfo'))
                if any(current.get(k) != expected.get(k) for k in ('TITLE_ID', 'TITLE')):
                    raise RuntimeError('TITLEID ocupado por otra app; no se escribió nada')
            self.report(self.command('quit all'))
            time.sleep(3)
            for local in sorted(root.rglob('*')):
                if local.is_file():
                    remote = APP + '/' + local.relative_to(root).as_posix()
                    self.put(local, remote, force=True)
            for local in sorted(stage.rglob('*')):
                if local.is_file():
                    rel = local.relative_to(stage).as_posix()
                    # Runtime logs/frames no son datos para subir; saves existentes
                    # se preservan, incluso si difieren del stage original.
                    if rel.lower().endswith(('.log', '.new', '.previous', '.core')) or rel.lower() in ('progress.txt', 'core'):
                        continue
                    preserve = rel.lower() in ('data/menu/settings.dat', 'data/menu/highscore.dat')
                    self.put(local, DATA + '/' + rel, preserve=preserve, force=rel.lower() == "env.txt")
            self._upload_music(music)
            self.report(f'deploy: {self.sent} subidos, {self.skipped} omitidos')
            if not self.launch_verified():
                self.put(vpk, DATA + '/hotwheels-vita.vpk', force=True)
                self.report('Launch sin sonda de arranque fresca ni tras reboot: no se confirmó ejecución. Instalar ux0:data/hotwheels/hotwheels-vita.vpk y abrir HWMR00002 con VitaShell; VitaCompanion no promueve apps ni confirma avisos de pantalla.')
                return 3
            self.report('Runtime confirmó boot_probe.txt fresco con main; revisar hotwheels.log para resultado.')
            return 0


def companion(ip, command):
    with socket.create_connection((ip, 1338), timeout=10) as sock:
        sock.sendall((command + '\n').encode())
        sock.settimeout(3)
        response = bytearray()
        while True:
            try:
                block = sock.recv(4096)
            except socket.timeout:
                break
            if not block:
                break
            response.extend(block)
        return response.decode(errors='replace').strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip', default=os.getenv('VITA_IP'), required=not os.getenv('VITA_IP'))
    parser.add_argument('--vpk', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True, help='carpeta HotWheels.exe + DATA, no su padre')
    parser.add_argument('--data-only', action='store_true', help='sube VPK/stage a ux0:data/hotwheels sin instalar ni lanzar')
    parser.add_argument('--music', type=Path, default=None,
                        help='carpeta con Track*.wav (música MCI); por defecto ../music o ../../music del --data')
    args = parser.parse_args()
    if CONSOLE_DOWN.exists():
        parser.error("CONSOLE_DOWN: consola indisponible, seguir emulador")
    if not (args.data / 'HotWheels.exe').is_file():
        parser.error('faltan los datos stageados')
    music = args.music
    if music is None:
        # Layout extraído: <raíz>/extracted/original/Program_Files + <raíz>/extracted/music
        for candidate in (args.data.parent / 'music', args.data.parent.parent / 'music'):
            if candidate.is_dir():
                music = candidate
                break
    with ftplib.FTP() as ftp:
        ftp.connect(args.ip, 1337, timeout=30)
        ftp.login()
        deployer = Deploy(ftp, None if args.data_only else lambda c: companion(args.ip, c))
        return deployer.run(args.vpk, args.data, data_only=args.data_only, music=music)


if __name__ == '__main__':
    raise SystemExit(main())
