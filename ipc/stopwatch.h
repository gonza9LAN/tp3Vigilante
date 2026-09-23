// ============================================================================
// stopwatch.h — logging con timestamp
// ----------------------------------------------------------------------------
// Casi todos los ejercicios del taller se corrigen mirando CUÁNDO pasó cada
// cosa, no solo qué pasó. Este header imprime cada línea con los segundos
// transcurridos desde que arrancó el programa:
//
//     [ 3.21s] [etapa 0 ] recibí pieza 4
//
// Uso:
//
//     #include "stopwatch.h"
//
//     int main() {
//         start_stopwatch();                 // al principio de main(), UNA vez
//         log("padre", "creando los pipes");
//         ...
//     }
//
// Importante: llamen a start_stopwatch() ANTES del primer fork(), así todos los
// procesos comparten el mismo cero y los tiempos se pueden comparar entre sí.
// ============================================================================

#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <chrono>
#include <cstdio>
#include <string>

inline std::chrono::steady_clock::time_point& stopwatch_origin() {
    static std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    return t;
}

// Fija el cero del cronómetro. Llamar al principio de main().
inline void start_stopwatch() {
    stopwatch_origin() = std::chrono::steady_clock::now();
}

// Segundos transcurridos desde start_stopwatch().
inline double elapsed() {
    std::chrono::duration<double> d = std::chrono::steady_clock::now() - stopwatch_origin();
    return d.count();
}

// Imprime "[ 1.23s] [quien    ] mensaje" con flush inmediato.
//
// El flush no es opcional: sin él, la salida queda en el buffer y se mezcla
// entre procesos (o se duplica al forkear), y los tiempos que ven en pantalla
// no son los tiempos en que pasaron las cosas.
inline void log(const std::string& who, const std::string& message) {
    printf("[%6.2fs] [%-9s] %s\n", elapsed(), who.c_str(), message.c_str());
    fflush(stdout);
}

#endif  // STOPWATCH_H
