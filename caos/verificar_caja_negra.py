#!/usr/bin/env python3
"""Verifica que una caja negra cumpla el contrato del TP3.

    python3 caos/verificar_caja_negra.py <carpeta caja_negra>
    python3 caos/verificar_caja_negra.py <carpeta caja_negra> --vigilar 10

Sin opciones revisa los archivos una vez: formato de estado.txt (con FIN al
final), numeración de ordenes.txt, que la bitácora exista, que nave.pid
apunte a un proceso vivo o no exista.

Con --vigilar N lee estado.txt lo más rápido que puede durante N segundos y
cuenta cuántas veces lo encontró incompleto (sin FIN). Con una nave que
escribe el snapshot de forma atómica tiene que dar cero. Es la pregunta 1 de
la parte 2, automatizada.

También se importa desde caos.py.
"""
import os
import signal
import sys
import time
from pathlib import Path

ROLES = ("reactor", "soporte_vital", "navegacion", "comunicaciones")
STATES = ("OK", "ALERTA", "CRITICO", "CAIDO")
ORDERS_WITH_TARGET = ("REPARAR",)
ORDERS_WITH_ROLE = ()
ORDERS_ALONE = ("ABORTAR",)


class Snapshot:
    """Un estado.txt parseado."""

    def __init__(self):
        self.last_order = None
        self.subsystems = {}  # rol -> dict(state, level, pid)
        self.complete = False


def parse_snapshot(text):
    """Devuelve (Snapshot, [errores]). `complete` es False si falta el FIN."""
    e = Snapshot()
    errors = []
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]
    if not lines or lines[-1] != "FIN":
        errors.append("no termina en FIN (snapshot incompleto)")
        return e, errors
    e.complete = True
    for line in lines[:-1]:
        if line.startswith("ultima_orden="):
            try:
                e.last_order = int(line[13:])
            except ValueError:
                errors.append(f"ultima_orden no es un entero: {line!r}")
        elif line.startswith("subsistema="):
            fields = line[11:].split()
            if len(fields) != 4:
                errors.append(f"línea de subsistema con {len(fields)} campos, esperaba 4: {line!r}")
                continue
            role, state, level, pid = fields
            if state not in STATES:
                errors.append(f"{role}: estado desconocido {state!r}")
            if state == "CAIDO" and (level, pid) != ("-", "-"):
                errors.append(f"{role}: CAIDO tiene que llevar '- -' y lleva {level} {pid}")
            if level != "-":
                try:
                    float(level)
                except ValueError:
                    errors.append(f"{role}: nivel no es un número: {level!r}")
            if pid != "-":
                try:
                    pid = int(pid)
                except ValueError:
                    errors.append(f"{role}: pid no es un entero: {pid!r}")
            e.subsystems[role] = {"state": state, "level": level, "pid": pid}
        else:
            errors.append(f"línea desconocida: {line!r}")
    if e.last_order is None:
        errors.append("falta la línea ultima_orden=")
    elif e.last_order < 0:
        errors.append(f"ultima_orden tiene que ser >= 0, es {e.last_order}")
    if not e.subsystems:
        errors.append("no hay ninguna línea subsistema=")
    return e, errors


def read_snapshot(folder):
    """(Snapshot, errores) leyendo estado.txt; (None, [error]) si no existe."""
    path = Path(folder) / "estado.txt"
    try:
        text = path.read_text()
    except FileNotFoundError:
        return None, ["no existe estado.txt"]
    return parse_snapshot(text)


def verify_orders(folder):
    path = Path(folder) / "ordenes.txt"
    if not path.exists():
        return 0, []
    errors = []
    expected = 1
    for n, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split()
        if len(fields) < 2:
            errors.append(f"ordenes.txt línea {n}: {line!r} no tiene número y orden")
            continue
        try:
            seq = int(fields[0])
        except ValueError:
            errors.append(f"ordenes.txt línea {n}: {fields[0]!r} no es un número")
            continue
        if seq != expected:
            errors.append(f"ordenes.txt línea {n}: esperaba el número {expected}, hay {seq}")
        expected = seq + 1
        order = fields[1]
        arg = fields[2] if len(fields) > 2 else None
        if order in ORDERS_WITH_TARGET:
            if arg is None or (arg != "todos" and arg not in ROLES):
                errors.append(f"ordenes.txt línea {n}: {order} necesita un rol o 'todos'")
        elif order in ORDERS_WITH_ROLE:
            if arg not in ROLES:
                errors.append(f"ordenes.txt línea {n}: {order} necesita un rol")
        elif order in ORDERS_ALONE:
            if arg is not None:
                errors.append(f"ordenes.txt línea {n}: {order} no lleva argumento")
        else:
            errors.append(f"ordenes.txt línea {n}: orden desconocida {order!r}")
    return expected - 1, errors


def process_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def read_pid(folder):
    path = Path(folder) / "nave.pid"
    try:
        return int(path.read_text().split()[0])
    except (FileNotFoundError, ValueError, IndexError):
        return None


def verify(folder):
    """Revisa todo una vez. Devuelve la lista de errores (vacía si está bien)."""
    folder = Path(folder)
    errors = []
    if not folder.is_dir():
        return [f"{folder} no es una carpeta"]

    snapshot, errs = read_snapshot(folder)
    errors += [f"estado.txt: {e}" for e in errs]

    last_seq, errs = verify_orders(folder)
    errors += errs
    if snapshot and snapshot.complete and snapshot.last_order is not None and snapshot.last_order > last_seq:
        errors.append(f"estado.txt dice ultima_orden={snapshot.last_order} pero ordenes.txt llega hasta {last_seq}")

    if not (folder / "bitacora.log").exists():
        errors.append("no existe bitacora.log")
    elif (folder / "bitacora.log").stat().st_size == 0:
        errors.append("bitacora.log está vacía")

    pid = read_pid(folder)
    if (folder / "nave.pid").exists() and pid is None:
        errors.append("nave.pid existe pero no tiene un número")
    elif pid is not None and not process_alive(pid):
        errors.append(f"nave.pid apunta al pid {pid}, que no existe (¿murió sin limpiar?)")
    elif pid is not None and snapshot and snapshot.complete:
        for role, s in snapshot.subsystems.items():
            if s["state"] != "CAIDO" and isinstance(s["pid"], int) and not process_alive(s["pid"]):
                errors.append(f"estado.txt dice que {role} está {s['snapshot']} con pid {s['pid']}, pero ese proceso no existe")
    return errors


def watch(folder, seconds):
    """Lee estado.txt sin parar durante `segundos`. Devuelve un dict con lo visto."""
    path = Path(folder) / "estado.txt"
    deadline = time.time() + seconds
    reads = incomplete = missing_reads = 0
    while time.time() < deadline:
        reads += 1
        try:
            text = path.read_text()
        except FileNotFoundError:
            missing_reads += 1
            continue
        e, _ = parse_snapshot(text)
        if not e.complete:
            incomplete += 1
    return {"reads": reads, "incomplete": incomplete, "missing_reads": missing_reads}


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    folder = args[0]
    seconds = 0
    if "--vigilar" in args:
        seconds = float(args[args.index("--vigilar") + 1])

    errors = verify(folder)
    if errors:
        print(f"caja negra en {folder}: {len(errors)} problema(s)")
        for e in errors:
            print("  -", e)
    else:
        e, _ = read_snapshot(folder)
        print(f"caja negra en {folder}: OK (última orden #{e.last_order}, {len(e.subsystems)} subsistemas)")

    if seconds > 0:
        print(f"vigilando estado.txt durante {seconds:g}s...")
        v = watch(folder, seconds)
        print(f"  {v['reads']} lecturas, {v['incomplete']} incompletas (sin FIN), {v['missing_reads']} veces no existía")
        if v["incomplete"]:
            print("  PROBLEMA: un lector puede ver un snapshot roto. ¿Están escribiendo con rename()?")
            return 1
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
