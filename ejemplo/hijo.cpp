// ============================================================================
// ejemplo/hijo.cpp — un programa cualquiera que habla por stdin/stdout
// ----------------------------------------------------------------------------
// No sabe nada de pipes. Lee líneas de su entrada estándar, las devuelve en
// mayúsculas por su salida estándar, y si le llega SIGTERM se despide y sale.
//
// Probalo solo, con el teclado:
//
//     ./ejemplo_hijo
//     hola            <- escribís
//     HOLA (4)        <- responde
//     Ctrl+D          <- fin de la entrada: termina con 0
//
// Y después mirá ejemplo/lanzar.cpp, que lo corre "enchufado" a dos pipes en
// vez de a la terminal. El programa no nota la diferencia: eso es dup2.
//
// A propósito NO usa las funciones de ipc/, que todavía tienen que completar.
// Usa read() y write() a mano, para que el ejemplo ande desde el primer día.
// ============================================================================

#include <cctype>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>

volatile sig_atomic_t asked_to_exit = 0;
void on_sigterm(int) { asked_to_exit = 1; }

int main() {
    // sigaction sin SA_RESTART: si estamos dormidos en read(), SIGTERM lo corta
    // y read() devuelve -1 con errno == EINTR. Ahí miramos la bandera.
    struct sigaction action {};
    action.sa_handler = on_sigterm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGTERM, &action, nullptr);

    // Los logs van por stderr, que NO está enchufado al pipe: llegan a la
    // terminal aunque stdout esté redirigido. Es la forma de ver qué hace un
    // hijo sin ensuciar el canal.
    std::cerr << "[hijo " << getpid() << "] arranqué, espero líneas por stdin\n";

    std::string pending;  // lo leído que todavía no forma una línea completa
    int processed = 0;

    while (!asked_to_exit) {
        char buf[256];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n == 0) {
            std::cerr << "[hijo " << getpid() << "] me cerraron la entrada, chau\n";
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;  // fue una señal: el while decide
            perror("read");
            return 1;
        }
        pending.append(buf, static_cast<size_t>(n));

        // read() devuelve bytes, no líneas: puede venir media línea o dos
        // juntas. Procesamos todas las líneas completas que haya.
        size_t cut;
        while ((cut = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, cut);
            pending.erase(0, cut + 1);
            for (char& c : line) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            ++processed;

            // Respondemos por stdout con write(): NO usamos cout acá, porque
            // cout tiene su propio buffer y la respuesta podría quedarse ahí
            // sin llegar al otro proceso.
            std::string reply = line + " (" + std::to_string(line.size()) + ")\n";
            write(STDOUT_FILENO, reply.data(), reply.size());
        }
    }

    if (asked_to_exit) {
        std::cerr << "[hijo " << getpid() << "] me llegó SIGTERM, cierro ordenadamente\n";
    }
    std::cerr << "[hijo " << getpid() << "] procesé " << processed << " líneas\n";
    return 0;
}
