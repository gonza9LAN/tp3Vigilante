// ============================================================================
// ejemplo/lanzar.cpp — fork + exec + dup2: lanzar OTRO programa y conversar
// ----------------------------------------------------------------------------
// Este es el patrón que usa la nave para lanzar cada subsistema:
//
//     1. crear dos pipes (uno por sentido)
//     2. fork()
//     3. en el hijo: enchufar los pipes en stdin (0) y stdout (1) con dup2(),
//        cerrar todo lo demás, y exec() del programa
//     4. en el padre: cerrar las puntas que no usa y hablar por las suyas
//
// El programa lanzado no sabe que está hablando con un pipe. Cree que es la
// terminal. Por eso se puede probar a mano primero y enchufar después.
//
//     ./ejemplo_lanzar ./ejemplo_hijo
//
// Compará la salida con lo que hace ./ejemplo_hijo solo: es el mismo programa.
//
// A propósito NO usa las funciones de ipc/, que todavía tienen que completar.
// ============================================================================

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Lee del descriptor hasta encontrar un '\n' (bloqueante, de a un byte: lento
// pero suficiente para un ejemplo). Devuelve false si el otro lado cerró.
static bool read_one_line(int fd, std::string& line) {
    line.clear();
    char c;
    while (true) {
        ssize_t n = read(fd, &c, 1);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (c == '\n') return true;
        line += c;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <programa> [args...]\n";
        return 1;
    }

    // Si el hijo muere y escribimos igual, queremos EPIPE, no morir en silencio.
    signal(SIGPIPE, SIG_IGN);

    // Dos pipes: uno para hablarle al hijo y otro para escucharlo.
    int to_child[2];    // padre escribe en [1], hijo lee de [0] (su stdin)
    int from_child[2];  // hijo escribe en [1] (su stdout), padre lee de [0]
    if (pipe(to_child) == -1 || pipe(from_child) == -1) {
        perror("pipe");
        return 1;
    }

    std::cout.flush();  // siempre, antes de cada fork()

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // ============================ HIJO ==================================
        // dup2(viejo, nuevo): a partir de acá, el descriptor 0 ES la punta de
        // lectura del pipe, y el 1 ES la punta de escritura del otro.
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);

        // Ya están copiados en 0 y 1: cerramos los originales Y las puntas que
        // no son nuestras. Si el hijo se queda con to_child[1] abierto, nunca
        // va a ver EOF en su stdin, porque él mismo sería un escritor.
        //
        // Detalle fino: pipe() devuelve los descriptores más bajos que estén
        // libres. Si el proceso arrancó con stdin cerrado, to_child[0] puede
        // SER el 0, y cerrarlo sería cerrar el stdin que acabamos de armar.
        if (to_child[0] != STDIN_FILENO) close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        if (from_child[1] != STDOUT_FILENO) close(from_child[1]);

        // execvp quiere un array de char* terminado en nullptr.
        std::vector<char*> args;
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        args.push_back(nullptr);

        execvp(args[0], args.data());

        // Si llegamos acá, el exec falló (el programa no existe, no es
        // ejecutable, etc). El hijo NO debe seguir corriendo el código del
        // padre: termina acá.
        perror("execvp");
        _exit(127);
    }

    // ============================== PADRE ===================================
    close(to_child[0]);    // no leemos de nuestro propio canal de salida
    close(from_child[1]);  // no escribimos en el canal de entrada
    int write_fd = to_child[1];
    int read_fd = from_child[0];

    std::cout << "[padre] lancé '" << argv[1] << "' con pid " << pid << "\n";

    std::string line;
    const char* messages[] = {"hola mundo", "chau"};
    for (const char* m : messages) {
        std::cout << "[padre] envío:  " << m << "\n";
        std::string msg = std::string(m) + "\n";
        write(write_fd, msg.data(), msg.size());

        // Bloqueante: nos dormimos hasta que el hijo conteste. Acá está bien,
        // porque no tenemos nada más que hacer mientras tanto.
        if (!read_one_line(read_fd, line)) {
            std::cout << "[padre] el hijo no contestó\n";
            break;
        }
        std::cout << "[padre] recibí: " << line << "\n";
    }

    // Le pedimos que termine con una señal, no cerrándole la entrada.
    std::cout << "[padre] le mando SIGTERM\n";
    kill(pid, SIGTERM);

    // waitpid nos cuenta cómo terminó: por exit() (y con qué código) o por
    // una señal (y cuál). Sin esto, el hijo queda zombie.
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        std::cout << "[padre] el hijo terminó con exit(" << WEXITSTATUS(status) << ")\n";
    } else if (WIFSIGNALED(status)) {
        std::cout << "[padre] al hijo lo mató la señal " << WTERMSIG(status) << "\n";
    }

    close(write_fd);
    close(read_fd);
    return 0;
}
