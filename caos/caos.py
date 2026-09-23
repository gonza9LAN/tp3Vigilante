#!/usr/bin/env python3
"""caos — la tormenta solar.

    python3 caos/caos.py build                      # 90 segundos, nivel 3
    python3 caos/caos.py build --duracion 180
    python3 caos/caos.py build --nivel 1            # solo muertes de subsistemas
    python3 caos/caos.py build --semilla 7          # la misma tormenta, otra vez

Lanza la nave desde la carpeta que le pasan, con una caja negra nueva, y
durante `duracion` segundos le hace cosas:

  nivel 1: mata subsistemas (SIGKILL o SIGTERM)
  nivel 2: además, ráfagas de órdenes con sus SIGUSR1
  nivel 3: además, mata la nave con kill -9 y la vuelve a lanzar

Mientras tanto lee estado.txt sin parar buscando snapshots rotos. Al final apaga la nave con SIGTERM y revisa que no quede nada.

Deja el reporte en <caja_negra>/reporte_caos.txt y termina con 0 si la nave
sobrevivió a todo, 1 si algo se rompió.
"""
import argparse
import os
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verificar_caja_negra import ROLES, read_snapshot, read_pid, process_alive, verify  # noqa: E402


class Storm:
    def __init__(self, build, box, duration, level, seed):
        self.build = Path(build).resolve()
        self.box = Path(box)
        self.duration = duration
        self.level = level
        self.seed = seed
        self.rng = random.Random(seed)
        self.start_time = time.time()

        self.proc = None
        self.seen_pids = set()
        self.birth = {}     # pid -> cuándo lo vimos por primera vez
        self.failures = []         # (t, texto)
        self.events = {}        # nombre -> cantidad
        self.subsystem_restarts = 0
        self.ship_restarts = 0
        self.watch_data = {"reads": 0, "incomplete": 0}
        self.stop_watcher = threading.Event()
        self.expect_ship_alive = True

    # ------------------------------------------------------------------ util
    def t(self):
        return time.time() - self.start_time

    def say(self, text):
        print(f"[{self.t():6.1f}s] {text}", flush=True)

    def fail(self, text):
        self.failures.append((self.t(), text))
        self.say("FALLA: " + text)

    def count_event(self, name):
        self.events[name] = self.events.get(name, 0) + 1

    def snapshot(self):
        e, _ = read_snapshot(self.box)
        if e and e.complete:
            for s in e.subsystems.values():
                if isinstance(s["pid"], int) and s["pid"] not in self.seen_pids:
                    self.seen_pids.add(s["pid"])
                    self.birth[s["pid"]] = time.time()
            return e
        return None

    def wait_for(self, condition, timeout, step=0.1):
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = condition()
            if r:
                return r
            time.sleep(step)
        return None

    def pid_of(self, role):
        e = self.snapshot()
        if not e or role not in e.subsystems:
            return None
        p = e.subsystems[role]["pid"]
        return p if isinstance(p, int) else None

    def ship_children(self):
        if not self.proc or self.proc.poll() is not None:
            return []
        r = subprocess.run(["pgrep", "-P", str(self.proc.pid)], capture_output=True, text=True)
        return [int(x) for x in r.stdout.split()]

    def mature_subsystem(self):
        """Un rol cuyo proceso lleva al menos 3 s vivo (para no pisar la espera antes de relanzar)."""
        e = self.snapshot()
        if not e:
            return None
        candidates = []
        for role, s in e.subsystems.items():
            p = s["pid"]
            if isinstance(p, int) and s["state"] != "CAIDO" and process_alive(p):
                if time.time() - self.birth.get(p, time.time()) >= 3.0:
                    candidates.append(role)
        return self.rng.choice(candidates) if candidates else None

    # ------------------------------------------------------------- la nave
    def launch_ship(self):
        output = open(self.box / "nave.salida", "a")
        args = [str(self.build / "nave"), "--caja_negra", str(self.box), "--falla", "0.002"]
        self.proc = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT,
                                     start_new_session=True, cwd=str(self.build))
        self.expect_ship_alive = True
        ok = self.wait_for(lambda: read_pid(self.box) == self.proc.pid and self.snapshot(), 6)
        if not ok:
            self.fail("la nave no escribió nave.pid y estado.txt en 6 segundos")
            return False
        return True

    def order(self, order, arg=None, notify=True):
        path = self.box / "ordenes.txt"
        seq = 0
        if path.exists():
            for line in path.read_text().splitlines():
                try:
                    seq = max(seq, int(line.split()[0]))
                except (ValueError, IndexError):
                    pass
        seq += 1
        with open(path, "a") as f:
            f.write(f"{seq} {order}" + (f" {arg}" if arg else "") + "\n")
        if notify:
            try:
                os.kill(self.proc.pid, signal.SIGUSR1)
            except ProcessLookupError:
                pass
        return seq

    # ------------------------------------------------------------ vigilante
    def watcher(self):
        path = self.box / "estado.txt"
        last_check = 0
        while not self.stop_watcher.is_set():
            d = self.watch_data
            d["reads"] += 1
            try:
                text = path.read_text()
            except FileNotFoundError:
                time.sleep(0.005)
                continue
            from verificar_caja_negra import parse_snapshot
            e, _ = parse_snapshot(text)
            if not e.complete:
                d["incomplete"] += 1
            # Cada medio segundo, ¿la nave sigue viva cuando debería?
            if time.time() - last_check > 0.5:
                last_check = time.time()
                if self.expect_ship_alive and self.proc and self.proc.poll() is not None:
                    self.fail(f"la nave murió sola con código {self.proc.returncode}")
                    self.expect_ship_alive = False
            time.sleep(0.002)

    # -------------------------------------------------------------- eventos
    def ev_kill_subsystem(self):
        role = self.mature_subsystem()
        if not role:
            return
        p = self.pid_of(role)
        sig = self.rng.choice([signal.SIGKILL, signal.SIGKILL, signal.SIGTERM])
        self.say(f"mato {role} (pid {p}) con {sig.name}")
        self.count_event("matar_subsistema")
        try:
            os.kill(p, sig)
        except ProcessLookupError:
            return
        def relaunched():
            e = self.snapshot()
            if not e:
                return False
            s = e.subsystems.get(role)
            return s and isinstance(s["pid"], int) and s["pid"] != p and s["state"] != "CAIDO" and process_alive(s["pid"])
        if self.wait_for(relaunched, 8):
            self.subsystem_restarts += 1
            self.say(f"  {role} volvió con pid {self.pid_of(role)}")
        else:
            self.fail(f"{role} (pid {p}) murió y la nave no lo relanzó en 8 segundos")

    def ev_order_burst(self):
        k = self.rng.randint(3, 6)
        self.say(f"ráfaga de {k} órdenes con {k} SIGUSR1 seguidos")
        self.count_event("rafaga_ordenes")
        last = None
        for _ in range(k):
            last = self.order("REPARAR", self.rng.choice(list(ROLES) + ["todos"]))
        if self.wait_for(lambda: (lambda e: e and e.last_order >= last)(self.snapshot()), 6):
            self.say(f"  procesadas hasta la #{last}")
        else:
            e = self.snapshot()
            self.fail(f"mandé órdenes hasta la #{last} y la nave se quedó en la #{e.last_order if e else '?'}")

    def ev_kill_ship(self):
        e = self.snapshot()
        if not e:
            return
        old_pids = [s["pid"] for s in e.subsystems.values() if isinstance(s["pid"], int)]
        self.say(f"kill -9 a la nave (pid {self.proc.pid})")
        self.count_event("matar_nave")
        self.expect_ship_alive = False
        try:
            os.kill(self.proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.proc.wait(timeout=5)
        # Los subsistemas quedaron huérfanos: tienen que morirse solos.
        def orphans_dead():
            return not any(process_alive(p) for p in old_pids)
        if not self.wait_for(orphans_dead, 6):
            alive_pids = [p for p in old_pids if process_alive(p)]
            self.fail(f"la nave murió y estos subsistemas quedaron huérfanos vivos: {alive_pids}")
            for p in alive_pids:
                try:
                    os.kill(p, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        self.say("  relanzo la nave")
        if not self.launch_ship():
            return
        self.ship_restarts += 1
        def restarted():
            e = self.snapshot()
            return e and len(e.subsystems) == 4 and all(
                isinstance(s["pid"], int) and process_alive(s["pid"]) for s in e.subsystems.values())
        if self.wait_for(restarted, 8):
            self.say("  arrancó de nuevo con los cuatro subsistemas")
        else:
            self.fail("la nave relanzada no arrancó con los cuatro subsistemas vivos en 8 segundos")

    # ---------------------------------------------------------------- final
    def shutdown(self):
        self.say("fin de la tormenta: SIGTERM a la nave")
        children = self.ship_children()
        self.expect_ship_alive = False
        try:
            os.kill(self.proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            self.fail("al final la nave ya no existía")
            return
        try:
            code = self.proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.fail("la nave no terminó en 8 segundos después del SIGTERM")
            os.kill(self.proc.pid, signal.SIGKILL)
            self.proc.wait()
            return
        if code != 0:
            self.fail(f"la nave terminó con código {code} después del SIGTERM, esperaba 0")
        if (self.box / "nave.pid").exists():
            self.fail("después del apagado quedó nave.pid")
        time.sleep(0.5)
        alive_pids = [p for p in set(children) | self.seen_pids if process_alive(p)]
        if alive_pids:
            self.fail(f"después del apagado quedaron procesos vivos: {alive_pids}")
            for p in alive_pids:
                try:
                    os.kill(p, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        for err in verify(self.box):
            self.fail("caja negra: " + err)

    def run(self):
        self.say(f"tormenta solar nivel {self.level}, semilla {self.seed}, {self.duration:g}s, caja negra en {self.box}")
        if not self.launch_ship():
            return self.report()
        thread = threading.Thread(target=self.watcher, daemon=True)
        thread.start()

        events = [self.ev_kill_subsystem]
        if self.level >= 2:
            events.append(self.ev_order_burst)
        if self.level >= 3:
            events.append(self.ev_kill_ship)

        time.sleep(3.0)  # que arranque tranquila
        deadline = self.start_time + self.duration
        try:
            while time.time() < deadline:
                if self.proc.poll() is not None and self.expect_ship_alive:
                    break
                if self.proc.poll() is not None:
                    self.say("la nave no está; relanzo para seguir")
                    if not self.launch_ship():
                        break
                ev = self.rng.choice(events)
                ev()
                time.sleep(self.rng.uniform(1.0, 3.0))
        except KeyboardInterrupt:
            self.say("interrumpido")
        finally:
            self.stop_watcher.set()
            thread.join(timeout=2)
            if self.proc.poll() is None:
                self.shutdown()
        return self.report()

    def report(self):
        d = self.watch_data
        if d["incomplete"]:
            self.fail(f"leyendo estado.txt {d['reads']} veces vi {d['incomplete']} snapshots incompletos")
        lines = [
            "=== Reporte de la tormenta solar ===",
            f"nivel {self.level}, semilla {self.seed}, duración {self.duration:g}s",
            f"reinicios de subsistemas: {self.subsystem_restarts}",
            f"reinicios de la nave: {self.ship_restarts}",
            "eventos: " + (", ".join(f"{k} x{v}" for k, v in sorted(self.events.items())) or "ninguno"),
            f"lecturas de estado.txt: {d['reads']} ({d['incomplete']} incompletas)",
            "",
        ]
        if self.failures:
            lines.append(f"FALLAS: {len(self.failures)}")
            for t, text in self.failures:
                lines.append(f"  [{t:6.1f}s] {text}")
            lines.append("")
            lines.append("La nave NO sobrevivió a la tormenta.")
        else:
            lines.append("Sin fallas. La nave sobrevivió a la tormenta.")
        text = "\n".join(lines) + "\n"
        (self.box / "reporte_caos.txt").write_text(text)
        print("\n" + text)
        print(f"(reporte guardado en {self.box / 'reporte_caos.txt'})")
        return 1 if self.failures else 0


def main():
    ap = argparse.ArgumentParser(description="La tormenta solar del TP3.")
    ap.add_argument("build", help="carpeta con los ejecutables nave y subsistema")
    ap.add_argument("--duracion", dest="duration", type=float, default=90, help="segundos de tormenta (default 90)")
    ap.add_argument("--nivel", dest="level", type=int, default=3, choices=[1, 2, 3])
    ap.add_argument("--semilla", dest="seed", type=int, default=None, help="para repetir la misma tormenta")
    ap.add_argument("--caja_negra", dest="black_box", default=None, help="carpeta a usar (se VACÍA); default: una temporal")
    a = ap.parse_args()

    build = Path(a.build).resolve()
    if not (build / "nave").exists() or not (build / "subsistema").exists():
        print(f"no encuentro nave y subsistema en {build}. ¿Compilaron?")
        return 2

    if a.black_box:
        box = Path(a.black_box).resolve()
        if box.exists():
            shutil.rmtree(box)
        box.mkdir(parents=True)
    else:
        box = Path(tempfile.mkdtemp(prefix="tp3_caos_"))

    seed = a.seed if a.seed is not None else random.randrange(1, 10000)
    storm = Storm(build, box, a.duration, a.level, seed)
    return storm.run()


if __name__ == "__main__":
    sys.exit(main())
