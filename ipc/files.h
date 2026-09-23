// ============================================================================
// ipc/files.h — lo mínimo para usar un archivo como canal entre procesos
// ----------------------------------------------------------------------------
// En clase vimos que un archivo sirve para que dos procesos que no se
// conocen se comuniquen, y vimos sus problemas: no avisa, no delimita mensajes
// y no coordina escritores. Estas funciones resuelven los que tienen solución
// en el archivo mismo. Avisar es tarea de las señales.
//
// write_atomic() la completan ustedes (parte 0). Hay tests.
// ============================================================================

#ifndef FILES_H
#define FILES_H

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

// ----------------------------------------------------------------------------
// Reemplaza el contenido de `path` de forma ATÓMICA.
//
// El truco: escribir todo en un archivo temporal AL LADO (por ejemplo,
// `path + ".tmp"`) y después renombrarlo encima del original con rename().
// rename() es atómico en POSIX: cualquier proceso que abra `path` ve o bien
// la versión vieja completa o bien la nueva completa. Nunca una mezcla, nunca
// un archivo cortado a la mitad.
//
// Comparen con un ofstream común: mientras se escribe, el archivo existe
// truncado y creciendo, y un lector que llegue en ese momento lee basura.
//
// Devuelve false si algo falla, y en ese caso no tiene que quedar el temporal
// dando vueltas.
// ----------------------------------------------------------------------------
inline bool write_atomic(const std::string& path, const std::string& content) {
    // TODO (parte 0)
    (void)path;
    (void)content;
    return false;
}

// ----------------------------------------------------------------------------
// Lee el archivo entero. Devuelve false si no existe o no se puede leer.
// ----------------------------------------------------------------------------
inline bool read_file(const std::string& path, std::string& content) {
    std::ifstream in(path);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    content = ss.str();
    return true;
}

// ----------------------------------------------------------------------------
// Agrega una línea al final de `path` (la crea si no existe).
//
// O_APPEND garantiza que cada write() vaya al final del archivo TAL COMO ESTÁ en ese
// instante, aunque otro proceso haya escrito en el medio. Y como la línea
// (con su '\n') sale en un solo write_all(), dos procesos que agregan líneas
// a la vez no se intercalan a mitad de línea.
// ----------------------------------------------------------------------------
inline bool append_line(const std::string& path, const std::string& line) {
    int fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) {
        perror("open append");
        return false;
    }
    // Un solo write() por línea, insistiendo si escribió de menos. (No usa
    // write_all() para que esta función ande antes de la parte 0.)
    std::string data = line + "\n";
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }
    close(fd);
    return true;
}

// ----------------------------------------------------------------------------
// Lee un PID de un archivo (la primera línea). Devuelve -1 si el archivo no
// existe o no tiene un número.
// ----------------------------------------------------------------------------
inline pid_t read_pid(const std::string& path) {
    std::string content;
    if (!read_file(path, content)) return -1;
    char* end_ptr = nullptr;
    long value = strtol(content.c_str(), &end_ptr, 10);
    if (end_ptr == content.c_str() || value <= 0) return -1;
    return static_cast<pid_t>(value);
}

// ----------------------------------------------------------------------------
// ¿Existe un proceso con ese PID?
//
// kill() con señal 0 no manda nada: solo verifica que el proceso exista y que
// tengamos permiso para señalizarlo. Es la forma estándar de saber si un
// pidfile está vivo o quedó de una corrida anterior.
// ----------------------------------------------------------------------------
inline bool process_alive(pid_t pid) {
    if (pid <= 0) return false;
    if (kill(pid, 0) == 0) return true;
    return errno == EPERM;  // existe, pero es de otro usuario
}

#endif  // FILES_H
