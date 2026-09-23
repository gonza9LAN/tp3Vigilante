// ============================================================================
// ipc/util.h — las tres funciones que van a usar en todos lados
// ----------------------------------------------------------------------------
// Ninguna es "de la materia": son el mínimo indispensable para usar pipes sin
// romperse, y las van a llamar decenas de veces. Por eso son lo primero que
// tienen que completar (parte 0), y por eso hay tests que las verifican:
//
//     cmake --build build && (cd build && ctest --output-on-failure)
//
//   write_all()       -> write() puede escribir de menos. Hay que insistir.
//   read_line()       -> read() no devuelve líneas. Hay que armarlas.
//   set_nonblocking() -> el fcntl() bien hecho.
//
// Si hicieron el taller, ya escribieron las tres. Tráiganlas.
// ============================================================================

#ifndef UTIL_H
#define UTIL_H

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

// ----------------------------------------------------------------------------
// Escribe TODO el string en `fd`, insistiendo si write() escribió de menos.
//
// Devuelve true si salió todo. Devuelve false si el canal se rompió (write()
// dio -1 con un errno que no sea EINTR; el típico es EPIPE, "no queda lector").
//
// Si write() devuelve -1 con errno == EINTR es que una señal interrumpió la
// llamada: no es un error, hay que volver a intentar.
// ----------------------------------------------------------------------------
inline bool write_all(int fd, const std::string& data) {
    // TODO (parte 0)
    (void)fd;
    (void)data;
    return false;
}

// ----------------------------------------------------------------------------
// Resultado de intentar leer una línea de un descriptor.
// ----------------------------------------------------------------------------
enum class ReadResult {
    LINE,         // hay una línea completa en 'line'
    NOTHING,      // no hay datos por ahora (solo pasa en modo no bloqueante)
    END,          // EOF: se cerraron todos los extremos de escritura
    INTERRUPTED,  // una señal cortó el read() (EINTR): revisen sus banderas
    ERROR         // error de verdad
};

// ----------------------------------------------------------------------------
// Lee de `fd` hasta completar una línea (sin el '\n') y la deja en `line`.
//
// `pending` es el buffer acumulador de ESE descriptor: hay que tener uno por
// cada fd y mantenerlo entre llamadas. Ahí queda lo que se leyó de más (el
// principio de la línea siguiente) y lo que todavía no completa una línea.
//
// El algoritmo:
//   1. Si en `pending` ya hay un '\n', cortar ahí: la línea sale sin el '\n',
//      se saca de `pending`, y se devuelve LINE. No se lee nada del fd.
//   2. Si no, read() al fd:
//        - n > 0: agregar los bytes a `pending` y volver al paso 1
//        - n == 0: devolver END
//        - n < 0 y errno == EINTR: devolver INTERRUPTED
//        - n < 0 y errno == EAGAIN o EWOULDBLOCK: devolver NOTHING
//        - cualquier otro error: devolver ERROR
//
// Funciona igual en modo bloqueante y no bloqueante: en bloqueante nunca
// devuelve NOTHING, porque read() se duerme hasta que haya algo.
// ----------------------------------------------------------------------------
inline ReadResult read_line(int fd, std::string& pending, std::string& line) {
    // TODO (parte 0)
    (void)fd;
    (void)pending;
    (void)line;
    return ReadResult::ERROR;
}

// ----------------------------------------------------------------------------
// Pone un descriptor en modo no bloqueante SIN pisar los flags que ya tenía.
//
// Es un F_GETFL, un OR con O_NONBLOCK, y un F_SETFL. Devuelve false si alguno
// de los dos fcntl() falla. Ojo: fcntl(fd, F_SETFL, O_NONBLOCK) a secas borra
// los otros flags (como O_APPEND); el test lo verifica.
// ----------------------------------------------------------------------------
inline bool set_nonblocking(int fd) {
    // TODO (parte 0)
    (void)fd;
    return false;
}

#endif  // UTIL_H
