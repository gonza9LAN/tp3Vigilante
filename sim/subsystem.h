// ============================================================================
// sim/subsystem.h — la simulación de un subsistema de la nave
// ----------------------------------------------------------------------------
// Esta clase es la "física" del TP: modela cómo se desgasta un subsistema,
// cómo reacciona a una reparación y cuándo se rompe. NO la modifiquen: los
// tests la verifican tal como está, y el punto del trabajo no es la
// simulación sino cómo se comunican los procesos que la usan.
//
// La clase no sabe nada de procesos, pipes ni señales. Eso es de ustedes.
//
// Uso típico dentro del proceso `subsistema`:
//
//     Subsystem s("reactor", seed);
//     while (true) {
//         s.tick();                       // pasa el tiempo
//         if (s.failed()) { ... }         // se rompió: el proceso muere
//         emitir(s.summary());            // telemetría hacia la nave
//         if (llego_una_orden) s.apply_order(orden);
//         usleep(periodo);
//     }
// ============================================================================

#pragma once

#include <random>
#include <string>
#include <vector>

// Los cuatro roles que existen a bordo. Cualquier otro nombre es un error.
const std::vector<std::string>& valid_roles();

// Una foto del subsistema en un instante dado.
struct Telemetry {
    std::string role;
    long tick;
    double level;       // 0.0 a 100.0
    std::string state;  // "OK", "ALERTA" o "CRITICO"
};

class Subsystem {
public:
    // Umbrales que definen el estado a partir del nivel.
    static constexpr double ALERT_THRESHOLD = 30.0;
    static constexpr double CRITICAL_THRESHOLD = 10.0;

    // `seed` hace la simulación reproducible: dos subsistemas con el mismo rol
    // y la misma semilla, reparados en los mismos ticks, evolucionan igual.
    //
    // `failure_prob` es la probabilidad, en cada tick, de que el subsistema se
    // rompa. Con 0.0 no se rompe nunca (útil para debuggear). Cuando el nivel
    // llega a 0 la probabilidad se multiplica por 10: un subsistema descuidado
    // termina fallando.
    Subsystem(const std::string& role, unsigned seed, double failure_prob = 0.005);

    // Avanza la simulación un paso: el nivel baja y puede producirse una
    // falla. Si ya falló, no hace nada.
    void tick();

    // La única orden que entiende el subsistema es "REPARAR": el nivel vuelve
    // a 100. Devuelve false ante cualquier otra cosa, y también si el
    // subsistema ya falló.
    bool apply_order(const std::string& order);

    // true si el subsistema se rompió en algún tick. Es definitivo: no hay
    // orden que lo arregle. Lo único que se puede hacer es lanzar uno nuevo.
    bool failed() const;

    Telemetry telemetry() const;

    // Una línea legible con todo el estado, por ejemplo:
    //     reactor tick=12 nivel=87.3 estado=OK
    // Pueden usarla tal cual como mensaje de telemetría, o armar el suyo.
    std::string summary() const;

    const std::string& role() const;
    long ticks() const;
    double level() const;
    std::string state() const;

private:
    std::string role_;
    double base_drain_;
    double failure_prob_;
    std::mt19937 rng_;
    long ticks_ = 0;
    double level_ = 100.0;
    bool failed_ = false;
};
