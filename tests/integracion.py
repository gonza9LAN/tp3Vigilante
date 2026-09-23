#!/usr/bin/env python3
"""Tests de integración del TP3: levantan la nave de verdad y la castigan.

    python3 tests/integracion.py build            # todos
    python3 tests/integracion.py build reinicio   # uno solo
    python3 tests/integracion.py build -v         # con la salida de la nave si falla

Cada test arranca ./nave desde la carpeta que le pasan, con una caja negra
temporal, y verifica lo que dice el contrato de la consigna mirando SOLO lo que
es observable desde afuera: los archivos de la caja negra, los PIDs, los
códigos de salida y qué procesos quedan vivos.

No prueban el protocolo nave <-> subsistema (es de ustedes). Las órdenes se
escriben acá directamente, igual que lo hace tierra.
"""
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "caos"))
from verificar_caja_negra import read_snapshot, read_pid, process_alive, verify, watch  # noqa: E402

ROLES = ["reactor", "soporte_vital", "navegacion", "comunicaciones"]
VERBOSE = False
SHIPS = []  # las naves que creó el test en curso, para volcar su salida si falla


class Failure(Exception):
    pass


def wait_for(condition, timeout, step=0.1, what=""):
    """Espera hasta que condition() devuelva algo verdadero, o falla."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = condition()
        if r:
            return r
        time.sleep(step)
    raise Failure(f"esperé {timeout:g}s {what} y no pasó")


class Ship:
    """Una nave corriendo, con su caja negra temporal."""

    def __init__(self, build, failure=0):
        self.build = Path(build).resolve()
        self.box = Path(tempfile.mkdtemp(prefix="tp3_int_"))
        self.output = open(self.box / "nave.salida", "a")
        args = [str(self.build / "nave"), "--caja_negra", str(self.box), "--falla", str(failure)]
        # Sesión nueva: la nave es líder de su propio grupo, así podemos
        # simular un Ctrl+C con killpg sin tocar nada más.
        self.proc = subprocess.Popen(args, stdout=self.output, stderr=subprocess.STDOUT,
                                     start_new_session=True, cwd=str(self.build))
        self.seen_pids = set()
        SHIPS.append(self)

    @property
    def pid(self):
        return self.proc.pid

    def snapshot(self):
        e, _ = read_snapshot(self.box)
        if e and e.complete:
            for s in e.subsystems.values():
                if isinstance(s["pid"], int):
                    self.seen_pids.add(s["pid"])
        return e

    def complete_snapshot(self):
        e = self.snapshot()
        return e if (e and e.complete) else None

    def pid_of(self, role):
        e = self.complete_snapshot()
        if not e or role not in e.subsystems:
            return None
        p = e.subsystems[role]["pid"]
        return p if isinstance(p, int) else None

    def children(self):
        r = subprocess.run(["pgrep", "-P", str(self.pid)], capture_output=True, text=True)
        return [int(x) for x in r.stdout.split()]

    def order(self, order, arg=None):
        """Lo que hace `tierra ordenar`: agregar la línea y avisar con SIGUSR1."""
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
        os.kill(self.pid, signal.SIGUSR1)
        return seq

    def alive(self):
        return self.proc.poll() is None

    def wait_exit(self, timeout):
        try:
            return self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            raise Failure(f"la nave no terminó en {timeout:g}s")

    def shutdown_and_verify(self, sig=signal.SIGTERM, timeout=6):
        """Manda la señal y verifica el apagado ordenado del contrato."""
        children = self.children()
        self.snapshot()
        if sig is not None:
            os.kill(self.pid, sig)
        code = self.wait_exit(timeout)
        if code != 0:
            raise Failure(f"la nave terminó con código {code}, esperaba 0")
        if (self.box / "nave.pid").exists():
            raise Failure("después del apagado sigue existiendo nave.pid")
        time.sleep(0.3)
        alive_pids = [p for p in set(children) | self.seen_pids if process_alive(p)]
        if alive_pids:
            raise Failure(f"después del apagado quedaron procesos vivos: {alive_pids}")
        if not self.complete_snapshot():
            raise Failure("después del apagado no hay un estado.txt completo")

    def cleanup(self):
        """Mata todo lo que haya quedado. Se llama siempre, pase lo que pase."""
        if self.alive():
            try:
                os.killpg(os.getpgid(self.pid), signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pass
        for p in self.seen_pids:
            try:
                os.kill(p, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.output.close()

    def dump_output(self):
        try:
            text = (self.box / "nave.salida").read_text()
        except FileNotFoundError:
            return
        print("      --- salida de la nave (últimas 25 líneas) ---")
        for line in text.splitlines()[-25:]:
            print("      " + line)


def wait_started(n):
    """Espera a que la nave tenga nave.pid, un estado.txt completo y telemetría de los 4."""
    wait_for(lambda: read_pid(n.box) == n.pid, 4, what="a que aparezca nave.pid con el pid correcto")
    wait_for(n.complete_snapshot, 4, what="a que haya un estado.txt completo")
    def with_telemetry():
        e = n.complete_snapshot()
        return e and all(r in e.subsystems and e.subsystems[r]["level"] != "-" for r in ROLES)
    wait_for(with_telemetry, 4, what="a que los 4 subsistemas reporten nivel")


# ----------------------------------------------------------------------------
# Los tests. Cada uno recibe la carpeta build y devuelve un detalle (string).
# ----------------------------------------------------------------------------

def test_startup(build):
    """Arranca, escribe nave.pid y un estado.txt válido con los 4 subsistemas vivos; SIGTERM la apaga bien."""
    n = Ship(build)
    try:
        wait_started(n)
        e = n.complete_snapshot()
        for role in ROLES:
            s = e.subsystems[role]
            if s["state"] == "CAIDO" or not isinstance(s["pid"], int):
                raise Failure(f"{role} figura como {s['state']} con pid {s['pid']}")
            if not process_alive(s["pid"]):
                raise Failure(f"{role} figura con pid {s['pid']} pero ese proceso no existe")
        errors = verify(n.box)
        if errors:
            raise Failure("la caja negra no cumple el contrato: " + "; ".join(errors))
        n.shutdown_and_verify()
        return "4 subsistemas vivos y con telemetría, apagado limpio"
    finally:
        n.cleanup()


def test_ctrl_c_shutdown(build):
    """SIGINT a todo el grupo (lo que hace Ctrl+C) produce un apagado ordenado."""
    n = Ship(build)
    try:
        wait_started(n)
        pids = [n.pid_of(r) for r in ROLES]
        os.killpg(os.getpgid(n.pid), signal.SIGINT)
        n.shutdown_and_verify(sig=None)
        dead = [p for p in pids if p and not process_alive(p)]
        return f"SIGINT al grupo, salida 0, {len(dead)} subsistemas terminados"
    finally:
        n.cleanup()


def test_restart(build):
    """Si un subsistema muere (kill -9), la nave lo relanza con otro pid."""
    n = Ship(build)
    try:
        wait_started(n)
        p1 = n.pid_of("reactor")
        os.kill(p1, signal.SIGKILL)
        def relaunched():
            p = n.pid_of("reactor")
            e = n.complete_snapshot()
            return p and p != p1 and process_alive(p) and e.subsystems["reactor"]["state"] != "CAIDO"
        wait_for(relaunched, 6, what="a que el reactor vuelva con otro pid")
        p2 = n.pid_of("reactor")
        n.shutdown_and_verify()
        return f"reactor {p1} -> {p2}"
    finally:
        n.cleanup()


def test_orders(build):
    """Las órdenes de ordenes.txt + SIGUSR1 se procesan todas, en orden."""
    n = Ship(build)
    try:
        wait_started(n)
        # Dejamos que se gasten un poco y después reparamos a todos.
        time.sleep(2.0)
        n.order("REPARAR", "todos")
        def all_repaired():
            e = n.complete_snapshot()
            return e.last_order >= 1 and all(
                e.subsystems[r]["level"] != "-" and float(e.subsystems[r]["level"]) >= 97.0 for r in ROLES)
        wait_for(all_repaired, 5, what="a que los 4 vuelvan a nivel ~100")

        # Dos órdenes seguidas con dos señales seguidas: se tienen que procesar las dos.
        n.order("REPARAR", "reactor")
        n.order("REPARAR", "navegacion")
        wait_for(lambda: n.complete_snapshot().last_order >= 3, 5, what="a ultima_orden=3")

        n.order("ABORTAR")
        n.shutdown_and_verify(sig=None)
        return f"ultima_orden={n.complete_snapshot().last_order}, ABORTAR apagó la nave"
    finally:
        n.cleanup()


def test_atomic_snapshot(build):
    """Leyendo estado.txt sin parar, nunca se ve incompleto."""
    n = Ship(build)
    try:
        wait_started(n)
        v = watch(n.box, 5)
        if v["incomplete"]:
            raise Failure(f"en {v['reads']} lecturas vi {v['incomplete']} snapshots incompletos")
        n.shutdown_and_verify()
        return f"{v['reads']} lecturas, ninguna rota"
    finally:
        n.cleanup()


TESTS = [
    ("arranque", test_startup, 3),
    ("apagado_ctrl_c", test_ctrl_c_shutdown, 2),
    ("reinicio", test_restart, 3),
    ("ordenes", test_orders, 3),
    ("snapshot_atomico", test_atomic_snapshot, 2),
]


def main():
    global VERBOSE
    args = [a for a in sys.argv[1:] if a != "-v"]
    VERBOSE = "-v" in sys.argv
    if not args:
        print(__doc__)
        return 2
    build = Path(args[0]).resolve()
    if not (build / "nave").exists() or not (build / "subsistema").exists():
        print(f"no encuentro nave y subsistema en {build}. ¿Compilaron?")
        return 2
    selected = args[1:]

    print(f"Nave: {build / 'nave'}\n")
    failed_count = 0
    points = earned = 0
    for name, fn, p in TESTS:
        if selected and name not in selected:
            continue
        points += p
        t0 = time.time()
        SHIPS.clear()
        try:
            detail = fn(build)
            ok = True
        except Failure as e:
            detail = str(e)
            ok = False
        except Exception as e:  # un bug del test o algo muy roto
            detail = f"{type(e).__name__}: {e}"
            ok = False
        dur = time.time() - t0
        earned += p if ok else 0
        failed_count += 0 if ok else 1
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:<18} ({p}p) {dur:5.1f}s  {detail}")
        if not ok and VERBOSE:
            for ship in SHIPS:
                ship.dump_output()

    print(f"\n  {earned}/{points} puntos, {failed_count} test(s) fallados")
    return 1 if failed_count else 0


if __name__ == "__main__":
    sys.exit(main())
