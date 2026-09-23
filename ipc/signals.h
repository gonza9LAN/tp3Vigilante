// ============================================================================
// ipc/signals.h — instalar handlers sin sorpresas
// ----------------------------------------------------------------------------
// signal() tiene semántica distinta según el sistema (en algunos el handler se
// desregistra solo después de la primera señal, en otros no). sigaction() es
// predecible en todos lados, y es lo que usan los programas serios.
// ============================================================================

#ifndef SIGNALS_H
#define SIGNALS_H

#include <csignal>
#include <cstdio>
#include <string>

// ----------------------------------------------------------------------------
// Instala `handler` para la señal `sig` usando sigaction(): un struct sigaction
// con sa_handler = handler, sa_mask vacía (sigemptyset) y sa_flags = flags.
// Devuelve false si sigaction() falla.
//
// Sobre `flags`, hay uno que importa mucho en este TP: SA_RESTART.
//
//   - Sin SA_RESTART (el default acá, flags = 0): si la señal llega mientras
//     el proceso está dormido en un read() bloqueante, el read() se corta y
//     devuelve -1 con errno == EINTR. El programa vuelve a su loop, ve la
//     bandera que levantó el handler, y actúa. La señal "despierta" al proceso.
//
//   - Con SA_RESTART: el kernel reanuda el read() solo, como si nada. Más
//     cómodo si no les interesa reaccionar, pero un proceso bloqueado no se va
//     a enterar de la señal hasta que lleguen datos.
//
// Hay un test que verifica las dos cosas: que el handler corre, y que un
// read() bloqueante se corta con EINTR cuando llega la señal.
//
// Recuerden lo de siempre: adentro del handler, casi nada es seguro. Nada de
// cout, printf, malloc ni std::string. Levanten una `volatile sig_atomic_t`
// y hagan el trabajo en el loop principal.
// ----------------------------------------------------------------------------
inline bool install_handler(int sig, void (*handler)(int), int flags = 0) {
    // TODO (parte 0)
    (void)sig;
    (void)handler;
    (void)flags;
    return false;
}

// Ignorar una señal por completo (la típica: SIGPIPE, para que write() en un
// pipe sin lector devuelva EPIPE en vez de matar el proceso).
inline bool ignore_signal(int sig) { return install_handler(sig, SIG_IGN); }

// Volver al comportamiento por defecto de la señal.
inline bool restore_default(int sig) { return install_handler(sig, SIG_DFL); }

// Nombre legible, para los logs.
inline std::string signal_name(int sig) {
    switch (sig) {
        case SIGINT: return "SIGINT";
        case SIGTERM: return "SIGTERM";
        case SIGKILL: return "SIGKILL";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
        case SIGCHLD: return "SIGCHLD";
        case SIGPIPE: return "SIGPIPE";
        case SIGALRM: return "SIGALRM";
        case SIGSTOP: return "SIGSTOP";
        case SIGCONT: return "SIGCONT";
        case SIGHUP: return "SIGHUP";
        default: return "señal " + std::to_string(sig);
    }
}

#endif  // SIGNALS_H
