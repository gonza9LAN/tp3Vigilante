// ============================================================================
// subsistema — un subsistema de la nave, como proceso
// ----------------------------------------------------------------------------
//     ./subsistema <rol> [--periodo <ms>] [--semilla <n>] [--falla <p>]
//
// Habla SOLO por su entrada y salida estándar: recibe órdenes por stdin y
// emite telemetría por stdout. No sabe si del otro lado hay una terminal o la
// nave. Eso permite probarlo a mano antes de enchufarlo:
//
//     ./subsistema reactor --periodo 1000
//     (ven la telemetría; escriben REPARAR y Enter, y el nivel vuelve a 100)
//
// Lo que tiene que hacer (parte 1):
//   - cada `periodo` ms: tick() y una línea de telemetría por stdout
//   - leer órdenes de stdin SIN dejar de emitir telemetría mientras espera
//   - si failed() da true: terminar EN EL ACTO con código 2, sin despedirse.
//     Es un crash. La nave tiene que enterarse sola.
//   - si se le cierra la entrada (EOF): terminar con código 0
//   - SIGTERM: terminar el tick en curso y salir con código 0
//
// Los logs de debug van por stderr, que la nave NO captura.
// ============================================================================

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "ipc/signals.h"
#include "ipc/stopwatch.h"
#include "ipc/util.h"
#include "sim/subsystem.h"

struct Config {
    std::string role;
    int period_ms = 500;
    unsigned seed = 0;  // 0 = elegir una al azar
    double failure_prob = 0.005;
};

static bool parse_args(int argc, char* argv[], Config& cfg) {
    if (argc < 2) return false;
    cfg.role = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (i + 1 >= argc) return false;
        std::string value = argv[++i];
        if (arg == "--periodo") cfg.period_ms = atoi(value.c_str());
        else if (arg == "--semilla") cfg.seed = static_cast<unsigned>(atol(value.c_str()));
        else if (arg == "--falla") cfg.failure_prob = atof(value.c_str());
        else return false;
    }
    return cfg.period_ms > 0;
}

int main(int argc, char* argv[]) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        std::cerr << "Uso: " << argv[0] << " <rol> [--periodo <ms>] [--semilla <n>] [--falla <p>]\n"
                  << "Roles:";
        for (const auto& r : valid_roles()) std::cerr << " " << r;
        std::cerr << "\n";
        return 1;
    }
    if (cfg.seed == 0) cfg.seed = static_cast<unsigned>(getpid()) * 2654435761u;

    Subsystem sub = [&]() {
        try {
            return Subsystem(cfg.role, cfg.seed, cfg.failure_prob);
        } catch (const std::invalid_argument& e) {
            std::cerr << "subsistema: " << e.what() << "\n";
            exit(1);
        }
    }();

    start_stopwatch();
    std::cerr << "[" << cfg.role << " " << getpid() << "] arranco con semilla " << cfg.seed << "\n";

    // ------------------------------------------------------------------------
    // TODO: el loop principal. Arranquen por acá.
    //
    // Antes de escribir una línea, decidan: ¿cómo leen stdin sin dejar de hacer
    // tick cada `periodo` ms? Hay más de una forma correcta (revisen el taller
    // 3 y el comentario sobre SA_RESTART en ipc/signals.h). Documenten la que
    // eligieron en el informe.
    // ------------------------------------------------------------------------
    std::cerr << "[" << cfg.role << "] así se ve la telemetría: " << sub.summary() << "\n";
    std::cerr << "subsistema: falta implementar el loop principal\n";
    return 1;
}
