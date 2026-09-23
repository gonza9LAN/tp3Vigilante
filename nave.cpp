// ============================================================================
// nave — la computadora de a bordo
// ----------------------------------------------------------------------------
//     ./nave [--caja_negra <dir>] [--falla <p>]
//
// Es el proceso coordinador. Lanza un proceso `subsistema` por cada uno de los
// cuatro roles, conversa con cada uno por un par de pipes y mantiene la caja
// negra.
//
// La estructura ya está armada: la configuración, el struct Child con lo que
// la nave sabe de cada subsistema, la clase Ship, los handlers de señales, el
// tablero, la bitácora, el snapshot y el loop principal. Lo que falta son los
// seis métodos marcados con TODO al final del archivo, que es donde está el
// IPC. En el orden en que conviene hacerlos:
//
//   Parte 1:  launch()          fork + exec + dup2, como en ejemplo/lanzar.cpp
//             read_telemetry()  leer los cuatro pipes sin bloquearse
//             process_line()    entender lo que manda su subsistema
//             reap_children()   SIGCHLD: waitpid y relanzar
//             shutdown()        el apagado ordenado
//   Parte 2:  process_orders()  SIGUSR1: leer ordenes.txt
//
// Hasta que launch() esté, la nave arranca, dice que no pudo lanzar y sale.
// Hasta que write_atomic() (parte 0) esté, ni siquiera eso.
// ============================================================================

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "ipc/files.h"
#include "ipc/signals.h"
#include "ipc/stopwatch.h"
#include "ipc/util.h"
#include "sim/subsystem.h"

using namespace std;

// Cada subsistema hace un tick cada medio segundo.
constexpr int PERIOD_MS = 500;

// ----------------------------------------------------------------------------
// Configuración
// ----------------------------------------------------------------------------
struct Config {
    double failure_prob = 0.005;  // se le pasa a cada subsistema
    string black_box = "caja_negra";
    string subsystem_bin;  // ruta al ejecutable `subsistema`
};

// El ejecutable `subsistema` vive en la misma carpeta que `nave`.
static string next_to(const char* argv0, const string& name) {
    string path = argv0;
    size_t slash = path.rfind('/');
    if (slash == string::npos) return "./" + name;
    return path.substr(0, slash + 1) + name;
}

static bool parse_args(int argc, char* argv[], Config& cfg) {
    cfg.subsystem_bin = next_to(argv[0], "subsistema");
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (i + 1 >= argc) return false;
        string value = argv[++i];
        if (arg == "--caja_negra") cfg.black_box = value;
        else if (arg == "--falla") cfg.failure_prob = atof(value.c_str());
        else if (arg == "--bin") cfg.subsystem_bin = value;
        else return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Señales: tres banderas, tres handlers de una línea. Adentro de un handler no
// se hace nada más que esto; el trabajo lo hace el loop principal.
// ----------------------------------------------------------------------------
volatile sig_atomic_t g_shutdown = 0;  // SIGTERM / SIGINT
volatile sig_atomic_t g_orders = 0;    // SIGUSR1
volatile sig_atomic_t g_children = 0;  // SIGCHLD

void on_shutdown_signal(int) { g_shutdown = 1; }
void on_orders_signal(int) { g_orders = 1; }
void on_child_signal(int) { g_children = 1; }

// ----------------------------------------------------------------------------
// Lo que la nave sabe de cada subsistema.
// ----------------------------------------------------------------------------
struct Child {
    string role;
    pid_t pid = -1;
    int in_fd = -1;   // por acá le escribimos (su stdin)
    int out_fd = -1;  // por acá lo leemos (su stdout)
    string pending;   // buffer de framing de su salida, para read_line()

    // Última telemetría recibida.
    bool has_telemetry = false;
    double level = 0;
    string state = "-";
    double last_message = 0;  // elapsed() de la última línea recibida

    // Estado de vida.
    bool dead = false;
    string death_cause;
    double relaunch_at = 0;  // elapsed() a partir del cual se relanza

    bool alive() const { return pid > 0 && !dead; }
};

// ----------------------------------------------------------------------------
// La nave
// ----------------------------------------------------------------------------
class Ship {
public:
    explicit Ship(const Config& cfg) : cfg_(cfg) {}

    int run();

private:
    Config cfg_;
    vector<Child> children_;
    long last_order_ = 0;
    double last_dashboard_ = -10;
    bool shutting_down_ = false;

    string path(const string& name) const { return cfg_.black_box + "/" + name; }

    // Dados.
    void logbook(const string& message);
    void dashboard();
    string state_of(const Child& h) const;
    void send_line(Child& h, const string& line);
    void snapshot();
    void relaunch_pending();

    // Para completar (ver el final del archivo).
    bool launch(Child& h);
    void read_telemetry();
    void process_line(Child& h, const string& line);
    void reap_children();
    void process_orders();
    int shutdown(const string& reason);
};

// Una línea en la bitácora, con fecha y hora de verdad además del cronómetro.
void Ship::logbook(const string& message) {
    char date_str[32];
    time_t now = time(nullptr);
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char elapsed_str[32];
    snprintf(elapsed_str, sizeof(elapsed_str), "[%7.2fs]", elapsed());
    append_line(path("bitacora.log"), string(date_str) + " " + elapsed_str + " " + message);
}

string Ship::state_of(const Child& h) const {
    if (h.dead || h.pid == -1) return "CAIDO";
    if (!h.has_telemetry) return "OK";  // recién lanzado, todavía no habló
    return h.state;
}

void Ship::dashboard() {
    log("nave", "--- tablero ---");
    for (const Child& h : children_) {
        string state = state_of(h);
        char line[200];
        if (state == "CAIDO") {
            snprintf(line, sizeof(line), "  %-15s %-8s   -     murió con %s", h.role.c_str(), state.c_str(),
                     h.death_cause.c_str());
        } else if (!h.has_telemetry) {
            snprintf(line, sizeof(line), "  %-15s %-8s   -     arrancando (pid %d)", h.role.c_str(), state.c_str(),
                     h.pid);
        } else {
            snprintf(line, sizeof(line), "  %-15s %-8s %5.1f   hace %.1fs", h.role.c_str(), state.c_str(), h.level,
                     elapsed() - h.last_message);
        }
        log("nave", line);
    }
}

// Le escribe una línea a un subsistema por su stdin.
void Ship::send_line(Child& h, const string& line) {
    if (!h.alive() || h.in_fd == -1) return;
    if (!write_all(h.in_fd, line)) {
        logbook(h.role + ": no pude escribirle (" + strerror(errno) + ")");
    }
}

// estado.txt con el formato del contrato. write_atomic() es de la parte 0.
void Ship::snapshot() {
    string s = "ultima_orden=" + to_string(last_order_) + "\n";
    for (const Child& h : children_) {
        string state = state_of(h);
        char line[200];
        if (state == "CAIDO") {
            snprintf(line, sizeof(line), "subsistema=%s CAIDO - -\n", h.role.c_str());
        } else if (!h.has_telemetry) {
            snprintf(line, sizeof(line), "subsistema=%s %s - %d\n", h.role.c_str(), state.c_str(), h.pid);
        } else {
            snprintf(line, sizeof(line), "subsistema=%s %s %.1f %d\n", h.role.c_str(), state.c_str(), h.level, h.pid);
        }
        s += line;
    }
    s += "FIN\n";
    if (!write_atomic(path("estado.txt"), s)) logbook("no pude escribir el snapshot");
}

// Relanza a los que murieron, cuando les toca.
void Ship::relaunch_pending() {
    if (shutting_down_) return;
    for (Child& h : children_) {
        if (h.pid != -1 || elapsed() < h.relaunch_at) continue;
        if (launch(h)) {
            logbook(h.role + ": relanzado con pid " + to_string(h.pid));
        } else {
            h.relaunch_at = elapsed() + 2.0;
        }
    }
}

// ============================================================================
// De acá para abajo, lo que completan ustedes. Cada método tiene arriba lo que
// tiene que hacer.
// ============================================================================

// Lanza el proceso `subsistema` para el rol de `h`, con dos pipes: uno para
// escribirle (su stdin) y otro para leerlo (su stdout). Es el patrón de
// ejemplo/lanzar.cpp, más tres cosas:
//   - el extremo por el que la nave lee va en modo no bloqueante
//   - el hijo ignora SIGINT antes del exec (es la pregunta 2 de la parte 1)
//   - el fork() también le copia al hijo los pipes de los OTROS subsistemas.
//     Cada subsistema tiene que quedar solo con sus descriptores 0 y 1
//     (pista: FD_CLOEXEC con fcntl, o cerrarlos a mano en el hijo)
// El comando es:
//     <cfg_.subsystem_bin> <rol> --periodo <PERIOD_MS> --falla <cfg_.failure_prob>
// Al volver, h.pid, h.in_fd y h.out_fd tienen que estar cargados y el resto de
// los campos de h reseteados (pending, has_telemetry, dead, death_cause, ...).
bool Ship::launch(Child& h) {
    // TODO (parte 1)
    (void)h;
    return false;
}

// Lee TODO lo que haya disponible en el pipe de cada subsistema, sin bloquear
// nunca: read_line() sobre h.out_fd con h.pending, hasta que devuelva NOTHING.
// Cada LINE va a process_line(). Un END significa que el subsistema cerró su
// salida: murió o está por morir. Márquenlo caído (dead, death_cause) y dejen
// de escucharlo (close(h.out_fd) y h.out_fd = -1). El waitpid de
// reap_children() va a decir cómo murió.
void Ship::read_telemetry() {
    // TODO (parte 1)
}

// Una línea que mandó el subsistema `h`. El formato es de ustedes: lo definen
// en subsistema_main.cpp y lo documentan en el informe. Acá se actualizan
// h.last_message, h.has_telemetry, h.level y h.state.
void Ship::process_line(Child& h, const string& line) {
    // TODO (parte 1)
    (void)h;
    (void)line;
}

// Llegó SIGCHLD. Preguntar por TODOS los hijos con waitpid(-1, &status,
// WNOHANG) en un loop, hasta que devuelva 0 o -1: la señal no dice qué hijo
// murió ni cuántos, y dos SIGCHLD seguidos se pueden fusionar en uno. Por cada
// hijo que terminó: anotar en la bitácora cómo (WIFEXITED y WEXITSTATUS, o
// WIFSIGNALED y WTERMSIG), cerrar sus dos descriptores, marcarlo caído
// (pid = -1, dead = true, death_cause) y programar el relanzamiento en
// h.relaunch_at, por ejemplo medio segundo después. relaunch_pending() lo
// relanza cuando llega el momento.
void Ship::reap_children() {
    // TODO (parte 1)
}

// Llegó SIGUSR1: hay órdenes nuevas en ordenes.txt. Leer el archivo entero
// (read_file) y procesar TODAS las líneas cuyo número sea mayor que
// last_order_. Nunca "una por señal": la señal no cuenta. Actualizar
// last_order_ y anotar cada orden en la bitácora. Las órdenes son dos:
//     REPARAR <rol|todos>  ->  send_line() a ese subsistema, o a todos
//     ABORTAR              ->  shutting_down_ = true; el loop apaga
void Ship::process_orders() {
    // TODO (parte 2)
}

// El apagado ordenado del contrato: SIGTERM a cada subsistema vivo; esperar
// hasta dos segundos a que terminen (waitpid con WNOHANG, en un loop con
// usleep); SIGKILL a los que sigan, y waitpid de esos también; cerrar todos
// los descriptores; un último snapshot() y una línea en la bitácora; borrar
// nave.pid con unlink(); devolver 0. Después de esto no puede quedar ningún
// proceso vivo ni ningún zombie.
int Ship::shutdown(const string& reason) {
    // TODO (parte 1)
    logbook("apagando: " + reason);
    unlink(path("nave.pid").c_str());
    return 0;
}

// ============================================================================
// El loop principal (dado). Fíjense en el orden: primero bajar la bandera,
// después atender. Si llega otra señal en el medio, se ve en la próxima vuelta.
// ============================================================================
int Ship::run() {
    start_stopwatch();

    if (mkdir(cfg_.black_box.c_str(), 0755) == -1 && errno != EEXIST) {
        perror("mkdir caja_negra");
        return 1;
    }
    if (!write_atomic(path("nave.pid"), to_string(getpid()) + "\n")) {
        log("nave", "no pude escribir nave.pid (¿write_atomic está implementada?)");
        return 1;
    }

    ignore_signal(SIGPIPE);  // escribirle a un subsistema muerto da EPIPE, no muerte
    install_handler(SIGTERM, on_shutdown_signal);
    install_handler(SIGINT, on_shutdown_signal);
    install_handler(SIGUSR1, on_orders_signal);
    install_handler(SIGCHLD, on_child_signal);

    logbook("nave encendida, pid " + to_string(getpid()));
    log("nave", "computadora de a bordo, pid " + to_string(getpid()) + ", caja negra en " + cfg_.black_box);

    for (const string& role : valid_roles()) {
        Child h;
        h.role = role;
        children_.push_back(h);
    }
    for (Child& h : children_) {
        if (!launch(h)) {
            log("nave", "no pude lanzar " + h.role);
            return 1;
        }
        logbook(h.role + ": lanzado con pid " + to_string(h.pid));
    }
    snapshot();

    while (true) {
        if (g_shutdown) return shutdown("señal de apagado");
        if (shutting_down_) return shutdown("orden ABORTAR");

        if (g_children) {
            g_children = 0;
            reap_children();
        }

        read_telemetry();

        if (g_orders) {
            g_orders = 0;
            process_orders();
        }

        relaunch_pending();

        if (elapsed() - last_dashboard_ >= 1.0) {
            last_dashboard_ = elapsed();
            dashboard();
            snapshot();
        }

        usleep(20 * 1000);
    }
}

int main(int argc, char* argv[]) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        cerr << "Uso: " << argv[0] << " [--caja_negra <dir>] [--falla <p>] [--bin <ruta a subsistema>]\n";
        return 1;
    }
    Ship ship(cfg);
    return ship.run();
}
