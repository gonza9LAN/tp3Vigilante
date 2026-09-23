# TP3: la computadora de a bordo

Guía para compilar, correr y probar este TP. La consigna está en `consigna.pdf`.

## Estructura

- `nave.cpp`: la estructura ya está armada. Faltan seis métodos marcados con
  `TODO`, que es donde está el IPC. El encabezado dice en qué orden hacerlos.
- `subsistema_main.cpp`: el parseo y la creación del `Subsystem`. Falta el loop.
- `tierra.cpp`: **viene hecho**. Léanlo.
- `ipc/`: los helpers de IPC. Cinco funciones están **vacías y las completan
  ustedes en la parte 0**; los tests de `ctest` las verifican. El resto viene
  hecho.
- `sim/`: la clase `Subsystem`, que simula el desgaste y las fallas. **No la
  modifiquen.**
- `ejemplo/`: `lanzar.cpp` e `hijo.cpp`, el patrón `fork` + `exec` + `dup2` para
  lanzar otro programa y hablarle por pipes. Anda desde el primer día.
- `tests/`: `tests.cpp` prueba `ipc/` y `Subsystem`; `integracion.py` prueba
  el contrato con la nave corriendo. **No los modifiquen.**
- `caos/`: la tormenta solar (`caos.py`) y el verificador de la caja negra.
- `caja_negra/`: la crea la nave al correr. Está en el `.gitignore`.

## Compilar

```bash
cmake -S . -B build
cmake --build build
```

Targets: `nave`, `subsistema`, `tierra`, `ejemplo_lanzar`, `ejemplo_hijo`,
`tp_tests`. Para uno solo: `cmake --build build --target nave`.

## Correr

Todo se corre desde `build/`, porque la nave busca a `subsistema` en su misma
carpeta.

```bash
cd build
./nave                # caja negra en ./caja_negra
./nave --falla 0      # subsistemas que no se rompen solos, para debuggear
```

En otra terminal, también desde `build/`:

```bash
./tierra ver                    # el tablero de Control de Misión
./tierra ordenar REPARAR todos
./tierra ordenar REPARAR reactor
./tierra ordenar ABORTAR
```

Un subsistema se puede probar solo, con el teclado:

```bash
./subsistema reactor --periodo 1000
REPARAR
```

Y el ejemplo de `fork` + `exec` + `dup2`:

```bash
./ejemplo_lanzar ./ejemplo_hijo
```

## Tests

Los de `ctest` prueban las funciones de `ipc/` (fallan hasta que las completen)
y la clase `Subsystem` (pasan desde el principio):

```bash
cmake --build build --target tp_tests
cd build && ctest --output-on-failure
```

Los de integración levantan la nave de verdad, le hacen cosas (matan un
subsistema, le mandan órdenes, le hacen Ctrl+C) y verifican el contrato mirando
la caja negra y los procesos. Fallan hasta que implementen cada parte:

```bash
python3 tests/integracion.py build             # todos
python3 tests/integracion.py build reinicio    # uno solo
python3 tests/integracion.py build -v          # muestra la salida de la nave si falla
```

Para revisar una caja negra a mano, o vigilar que el snapshot nunca se vea
roto (pregunta 1 de la parte 2):

```bash
python3 caos/verificar_caja_negra.py build/caja_negra
python3 caos/verificar_caja_negra.py build/caja_negra --vigilar 10
```

La tormenta solar, una vez que la nave anda:

```bash
python3 caos/caos.py build
```

## El contrato, en corto

Lo que es fijo, porque lo usan `tierra`, los tests y `caos`. El detalle está en
la consigna.

- `caja_negra/nave.pid`: el PID de la nave. Se crea al arrancar y se borra al
  apagarse bien.
- `caja_negra/estado.txt`: el snapshot, escrito de forma atómica al menos una
  vez por segundo, con la última línea `FIN`:

  ```text
  ultima_orden=7
  subsistema=reactor OK 71.3 4242
  subsistema=soporte_vital ALERTA 28.0 4243
  subsistema=navegacion CAIDO - -
  subsistema=comunicaciones CRITICO 8.1 4245
  FIN
  ```

- `caja_negra/bitacora.log`: una línea por evento, solo se agrega.
- `caja_negra/ordenes.txt`: una orden por línea, numeradas desde 1:
  `REPARAR <rol|todos>` o `ABORTAR`.
- Señales a la nave: `SIGTERM` o `SIGINT` apagan ordenadamente, `SIGUSR1` avisa
  que hay órdenes nuevas. Al subsistema: `SIGTERM` lo termina con código 0.
- Un subsistema que falla termina en el acto con código 2.

## Cuando algo queda colgado

```bash
ps -ef | grep -E 'nave|subsistema'   # ver qué quedó vivo
pkill -f subsistema; pkill -f nave   # matar todo
```
