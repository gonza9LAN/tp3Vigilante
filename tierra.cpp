// ============================================================================
// tierra — Control de Misión, en Puerto Kernel
// ----------------------------------------------------------------------------
//     ./tierra ver [--caja_negra <dir>]
//     ./tierra ordenar <ORDEN> [<rol>|todos] [--caja_negra <dir>]
//
// Este programa viene HECHO. Léanlo: es el ejemplo de cómo un proceso que no
// es pariente de la nave se comunica con ella usando solo la carpeta de la
// caja negra y una señal.
//
// Proceso suelto. Solo conoce la carpeta de la caja negra.
//
//   tierra ver       -> polling de estado.txt cada 500 ms (es el ejemplo 5 de
//                       la clase: el archivo no avisa, hay que preguntar)
//   tierra ordenar   -> agrega la orden a ordenes.txt y le avisa a la nave con
//                       SIGUSR1
// ============================================================================

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "ipc/files.h"
#include "sim/subsystem.h"

using namespace std;

struct Config {
    string command;
    string order;
    string target;
    string black_box = "caja_negra";
};

static bool parse_args(int argc, char* argv[], Config& cfg) {
    if (argc < 2) return false;
    cfg.command = argv[1];
    vector<string> positional;
    for (int i = 2; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--caja_negra") {
            if (i + 1 >= argc) return false;
            cfg.black_box = argv[++i];
        } else {
            positional.push_back(arg);
        }
    }
    if (cfg.command == "ver") return positional.empty();
    if (cfg.command == "ordenar") {
        if (positional.empty() || positional.size() > 2) return false;
        cfg.order = positional[0];
        if (positional.size() == 2) cfg.target = positional[1];
        return true;
    }
    return false;
}

static bool is_role(const string& s) {
    for (const string& r : valid_roles()) {
        if (r == s) return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// ver
// ----------------------------------------------------------------------------
static int view(const Config& cfg) {
    // Ctrl+C lo mata, y está bien: no tiene nada que limpiar.
    string snapshot_path = cfg.black_box + "/estado.txt";
    string pid_path = cfg.black_box + "/nave.pid";

    while (true) {
        string content;
        bool have = read_file(snapshot_path, content);
        pid_t pid = read_pid(pid_path);
        bool alive = process_alive(pid);

        // Limpiar pantalla y volver arriba (códigos ANSI).
        cout << "\033[2J\033[H";
        cout << "=== Control de Misión, Puerto Kernel ===\n";
        if (alive) {
            cout << "Nave: EN VUELO (pid " << pid << ")\n";
        } else if (pid > 0) {
            cout << "Nave: SIN SEÑAL (nave.pid apunta al pid " << pid << ", que no existe)\n";
        } else {
            cout << "Nave: APAGADA (no hay nave.pid)\n";
        }
        cout << "\n";

        if (!have) {
            cout << "(todavía no hay estado.txt en " << cfg.black_box << ")\n";
        } else if (content.size() < 4 || content.compare(content.size() - 4, 4, "FIN\n") != 0) {
            cout << "(estado.txt incompleto: no termina en FIN)\n";
        } else {
            stringstream ss(content);
            string line;
            while (getline(ss, line)) {
                if (line == "FIN") continue;
                if (line.rfind("subsistema=", 0) == 0) {
                    stringstream fields(line.substr(11));
                    string role, state, level, pid_str;
                    fields >> role >> state >> level >> pid_str;
                    printf("  %-15s %-8s %6s  pid %s\n", role.c_str(), state.c_str(), level.c_str(), pid_str.c_str());
                } else {
                    cout << "  " << line << "\n";
                }
            }
        }
        cout << "\n(Ctrl+C para salir)\n";
        cout.flush();
        usleep(500 * 1000);
    }
}

// ----------------------------------------------------------------------------
// ordenar
// ----------------------------------------------------------------------------
static int order_cmd(const Config& cfg) {
    const string& o = cfg.order;
    if (o == "REPARAR") {
        if (!(cfg.target == "todos" || is_role(cfg.target))) {
            cerr << "tierra: REPARAR necesita un rol o 'todos'\n";
            return 1;
        }
    } else if (o == "ABORTAR") {
        if (!cfg.target.empty()) {
            cerr << "tierra: ABORTAR no lleva argumento\n";
            return 1;
        }
    } else {
        cerr << "tierra: orden desconocida '" << o << "'\n";
        return 1;
    }

    // El número que sigue: el mayor que haya en el archivo, más uno.
    // (Si dos tierra corren en el mismo instante pueden repetir un número.
    // Para este TP alcanza; el problema real se resuelve con un cerrojo.)
    string orders_path = cfg.black_box + "/ordenes.txt";
    long seq = 0;
    string content;
    if (read_file(orders_path, content)) {
        stringstream ss(content);
        string line;
        while (getline(ss, line)) {
            long n = atol(line.c_str());
            if (n > seq) seq = n;
        }
    }
    seq++;
    string line = to_string(seq) + " " + o + (cfg.target.empty() ? "" : " " + cfg.target);
    if (!append_line(orders_path, line)) {
        cerr << "tierra: no pude escribir la orden (¿existe " << cfg.black_box << "?)\n";
        return 1;
    }
    cout << "orden #" << seq << " registrada: " << o << (cfg.target.empty() ? "" : " " + cfg.target) << "\n";

    // La orden ya está en el archivo. Ahora, el aviso.
    pid_t pid = read_pid(cfg.black_box + "/nave.pid");
    if (!process_alive(pid)) {
        cout << "la nave no está corriendo: la orden queda encolada para cuando arranque\n";
        return 0;
    }
    if (kill(pid, SIGUSR1) == -1) {
        perror("kill SIGUSR1");
        return 1;
    }
    cout << "aviso enviado a la nave (pid " << pid << ")\n";
    return 0;
}

int main(int argc, char* argv[]) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        cerr << "Uso: " << argv[0] << " ver [--caja_negra <dir>]\n"
             << "     " << argv[0] << " ordenar <ORDEN> [<rol>|todos] [--caja_negra <dir>]\n";
        return 1;
    }
    return cfg.command == "ver" ? view(cfg) : order_cmd(cfg);
}
