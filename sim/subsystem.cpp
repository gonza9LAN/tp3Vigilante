#include "subsystem.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {

struct Profile {
    const char* role;
    double drain_per_tick;  // cuánto nivel pierde por tick
};

// Cada subsistema se desgasta a un ritmo distinto. Con un tick cada 500 ms, el
// reactor pasa de 100 a ALERTA en un minuto si nadie lo repara.
const Profile PROFILES[] = {
    {"reactor", 0.6},
    {"soporte_vital", 0.4},
    {"navegacion", 0.3},
    {"comunicaciones", 0.5},
};

}  // namespace

const std::vector<std::string>& valid_roles() {
    static const std::vector<std::string> roles = {"reactor", "soporte_vital", "navegacion",
                                                   "comunicaciones"};
    return roles;
}

Subsystem::Subsystem(const std::string& role, unsigned seed, double failure_prob)
    : role_(role), base_drain_(-1.0), failure_prob_(failure_prob), rng_(seed) {
    for (const Profile& p : PROFILES) {
        if (role == p.role) base_drain_ = p.drain_per_tick;
    }
    if (base_drain_ < 0.0) throw std::invalid_argument("rol desconocido: " + role);
    if (failure_prob < 0.0 || failure_prob > 1.0) throw std::invalid_argument("failure_prob fuera de [0, 1]");
}

void Subsystem::tick() {
    if (failed_) return;
    ++ticks_;

    // El desgaste tiene un poco de ruido para que no sea una recta perfecta.
    std::uniform_real_distribution<double> noise(0.7, 1.3);
    level_ = std::max(0.0, level_ - base_drain_ * noise(rng_));

    // Y cada tick hay una chance de que algo se rompa.
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    double p = failure_prob_ * (level_ <= 0.0 ? 10.0 : 1.0);
    if (chance(rng_) < p) failed_ = true;
}

bool Subsystem::apply_order(const std::string& order) {
    if (failed_ || order != "REPARAR") return false;
    level_ = 100.0;
    return true;
}

bool Subsystem::failed() const { return failed_; }

Telemetry Subsystem::telemetry() const { return {role_, ticks_, level_, state()}; }

std::string Subsystem::summary() const {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s tick=%ld nivel=%.1f estado=%s", role_.c_str(), ticks_, level_, state().c_str());
    return buf;
}

const std::string& Subsystem::role() const { return role_; }
long Subsystem::ticks() const { return ticks_; }
double Subsystem::level() const { return level_; }

std::string Subsystem::state() const {
    if (level_ < CRITICAL_THRESHOLD) return "CRITICO";
    if (level_ < ALERT_THRESHOLD) return "ALERTA";
    return "OK";
}
