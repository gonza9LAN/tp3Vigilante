// ============================================================================
// Tests de la parte 0 (las funciones de ipc/ que completan ustedes) y de la
// clase Subsystem, que se entrega hecha. No los modifiquen.
//
// Los de Subsystem pasan desde el primer día. Los de ipc/ fallan hasta que
// completen cada función: úsenlos como guía.
//
// Los tests de integración (que la nave reinicie un subsistema, que Ctrl+C la
// apague en orden, que procese las órdenes, etc.) están en tests/integracion.py.
// ============================================================================

#include <gtest/gtest.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc/files.h"
#include "ipc/signals.h"
#include "ipc/util.h"
#include "sim/subsystem.h"

using namespace std;

// ----------------------------------------------------------------------------
// Subsystem (se entrega hecha)
// ----------------------------------------------------------------------------

TEST(Subsystem, ValidRoles) {
    ASSERT_EQ(valid_roles().size(), 4u);
    for (const auto& role : valid_roles()) {
        EXPECT_NO_THROW(Subsystem(role, 1));
    }
    EXPECT_THROW(Subsystem("cafetera", 1), invalid_argument);
    EXPECT_THROW(Subsystem("reactor", 1, 1.5), invalid_argument);
}

TEST(Subsystem, StartsHealthyAndFull) {
    Subsystem s("reactor", 42);
    EXPECT_EQ(s.role(), "reactor");
    EXPECT_EQ(s.ticks(), 0);
    EXPECT_DOUBLE_EQ(s.level(), 100.0);
    EXPECT_EQ(s.state(), "OK");
    EXPECT_FALSE(s.failed());
}

TEST(Subsystem, SameSeedSameHistory) {
    Subsystem a("navegacion", 7, 0.01), b("navegacion", 7, 0.01);
    for (int i = 0; i < 300; ++i) {
        a.tick();
        b.tick();
        ASSERT_DOUBLE_EQ(a.level(), b.level());
        ASSERT_EQ(a.failed(), b.failed());
    }
}

TEST(Subsystem, LevelDrops) {
    Subsystem s("reactor", 3, 0.0);
    for (int i = 0; i < 10; ++i) s.tick();
    EXPECT_EQ(s.ticks(), 10);
    EXPECT_LT(s.level(), 100.0);
    EXPECT_GT(s.level(), 90.0);
}

TEST(Subsystem, RepairRestoresToHundred) {
    Subsystem s("comunicaciones", 5, 0.0);
    for (int i = 0; i < 80; ++i) s.tick();
    ASSERT_LT(s.level(), 100.0);
    ASSERT_TRUE(s.apply_order("REPARAR"));
    EXPECT_DOUBLE_EQ(s.level(), 100.0);
    EXPECT_EQ(s.ticks(), 80);  // reparar no rebobina el tiempo
}

TEST(Subsystem, UnknownOrder) {
    Subsystem s("reactor", 1, 0.0);
    EXPECT_FALSE(s.apply_order("reparar"));   // es case-sensitive
    EXPECT_FALSE(s.apply_order("ABORTAR"));  // esa es de la nave, no del subsistema
    EXPECT_FALSE(s.apply_order(""));
}

TEST(Subsystem, StatesByLevel) {
    Subsystem s("reactor", 9, 0.0);
    EXPECT_EQ(s.state(), "OK");
    while (s.level() >= Subsystem::ALERT_THRESHOLD) s.tick();
    EXPECT_EQ(s.state(), "ALERTA");
    while (s.level() >= Subsystem::CRITICAL_THRESHOLD) s.tick();
    EXPECT_EQ(s.state(), "CRITICO");
    EXPECT_LT(s.ticks(), 400);
}

TEST(Subsystem, FailureIsFinal) {
    Subsystem s("reactor", 1, 1.0);  // falla en el primer tick, seguro
    s.tick();
    ASSERT_TRUE(s.failed());
    double level = s.level();
    s.tick();
    EXPECT_EQ(s.ticks(), 1);  // ya no avanza
    EXPECT_DOUBLE_EQ(s.level(), level);
    EXPECT_FALSE(s.apply_order("REPARAR"));
}

TEST(Subsystem, ZeroProbNeverFails) {
    Subsystem s("reactor", 1, 0.0);
    for (int i = 0; i < 5000; ++i) s.tick();
    EXPECT_FALSE(s.failed());
    EXPECT_DOUBLE_EQ(s.level(), 0.0);  // pero a esta altura está vacío
    EXPECT_EQ(s.state(), "CRITICO");
}

TEST(Subsystem, TelemetryAndSummary) {
    Subsystem s("navegacion", 4, 0.0);
    for (int i = 0; i < 20; ++i) s.tick();
    Telemetry t = s.telemetry();
    EXPECT_EQ(t.role, "navegacion");
    EXPECT_EQ(t.tick, 20);
    EXPECT_DOUBLE_EQ(t.level, s.level());
    EXPECT_EQ(t.state, "OK");
    string r = s.summary();
    EXPECT_NE(r.find("navegacion tick=20 nivel="), string::npos);
    EXPECT_NE(r.find("estado=OK"), string::npos);
}

// ----------------------------------------------------------------------------
// Parte 0: ipc/util.h
// ----------------------------------------------------------------------------

class Pipe : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_EQ(pipe(fds), 0); }
    void TearDown() override {
        if (fds[0] != -1) close(fds[0]);
        if (fds[1] != -1) close(fds[1]);
    }
    void close_writer() {
        close(fds[1]);
        fds[1] = -1;
    }
    int fds[2];
};

TEST_F(Pipe, WriteAllWritesEverything) {
    ASSERT_TRUE(write_all(fds[1], "hola\n"));
    char buf[16] = {};
    ASSERT_EQ(read(fds[0], buf, sizeof(buf)), 5);
    EXPECT_EQ(string(buf), "hola\n");
}

TEST_F(Pipe, WriteAllFailsWithoutReader) {
    signal(SIGPIPE, SIG_IGN);  // si no, el test entero moriría
    close(fds[0]);
    fds[0] = -1;
    EXPECT_FALSE(write_all(fds[1], "nadie me lee\n"));
    EXPECT_EQ(errno, EPIPE);
}

TEST_F(Pipe, ReadLineAssemblesLinesAndKeepsTheRest) {
    string pending, line;
    ASSERT_EQ(write(fds[1], "uno\ndos\ntr", 10), 10);

    // Un solo read() trajo dos líneas y media. Tienen que salir de a una.
    ASSERT_EQ(read_line(fds[0], pending, line), ReadResult::LINE);
    EXPECT_EQ(line, "uno");
    ASSERT_EQ(read_line(fds[0], pending, line), ReadResult::LINE);
    EXPECT_EQ(line, "dos");
    EXPECT_EQ(pending, "tr");  // la media línea espera en el buffer

    // Llega el resto de la tercera, y después se cierra el canal.
    ASSERT_EQ(write(fds[1], "es\n", 3), 3);
    close_writer();
    ASSERT_EQ(read_line(fds[0], pending, line), ReadResult::LINE);
    EXPECT_EQ(line, "tres");
    EXPECT_EQ(read_line(fds[0], pending, line), ReadResult::END);
}

TEST_F(Pipe, ReadLineReturnsNothingWhenNonblockingAndEmpty) {
    ASSERT_TRUE(set_nonblocking(fds[0]));
    string pending, line;
    EXPECT_EQ(read_line(fds[0], pending, line), ReadResult::NOTHING);
    ASSERT_EQ(write(fds[1], "ahora sí\n", 10), 10);
    ASSERT_EQ(read_line(fds[0], pending, line), ReadResult::LINE);
    EXPECT_EQ(line, "ahora sí");
    EXPECT_EQ(read_line(fds[0], pending, line), ReadResult::NOTHING);
}

TEST_F(Pipe, SetNonblockingSetsTheFlag) {
    ASSERT_TRUE(set_nonblocking(fds[0]));
    int flags = fcntl(fds[0], F_GETFL, 0);
    ASSERT_NE(flags, -1);
    EXPECT_TRUE(flags & O_NONBLOCK);
    EXPECT_FALSE(fcntl(fds[1], F_GETFL, 0) & O_NONBLOCK);  // el otro extremo no cambia
}

TEST(Util, SetNonblockingKeepsOtherFlags) {
    char path[] = "/tmp/tp3_flags_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_NE(fd, -1);
    close(fd);
    fd = open(path, O_WRONLY | O_APPEND);
    ASSERT_NE(fd, -1);
    ASSERT_TRUE(set_nonblocking(fd));
    int flags = fcntl(fd, F_GETFL, 0);
    EXPECT_TRUE(flags & O_NONBLOCK);
    EXPECT_TRUE(flags & O_APPEND) << "F_SETFL a secas borró O_APPEND: falta el F_GETFL";
    close(fd);
    unlink(path);
}

// ----------------------------------------------------------------------------
// Parte 0: ipc/files.h
// ----------------------------------------------------------------------------

class Files : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/tp3_tests_XXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        dir = tmpl;
    }
    void TearDown() override { system(("rm -rf " + dir).c_str()); }
    int files_in_dir() {
        int count = 0;
        DIR* d = opendir(dir.c_str());
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') ++count;
        }
        closedir(d);
        return count;
    }
    string dir;
};

TEST_F(Files, WriteAtomicLeavesContentAndNothingElse) {
    string path = dir + "/estado.txt";
    ASSERT_TRUE(write_atomic(path, "ultima_orden=1\nFIN\n"));
    string read_back;
    ASSERT_TRUE(read_file(path, read_back));
    EXPECT_EQ(read_back, "ultima_orden=1\nFIN\n");

    ASSERT_TRUE(write_atomic(path, "ultima_orden=2\nFIN\n"));
    ASSERT_TRUE(read_file(path, read_back));
    EXPECT_EQ(read_back, "ultima_orden=2\nFIN\n");

    EXPECT_EQ(files_in_dir(), 1) << "quedó un archivo temporal dando vueltas";
}

TEST_F(Files, WriteAtomicFailsCleanlyInMissingDir) {
    EXPECT_FALSE(write_atomic(dir + "/no_existe/estado.txt", "x"));
    EXPECT_EQ(files_in_dir(), 0);
}

TEST_F(Files, AppendLineAccumulates) {
    string path = dir + "/bitacora.log";
    ASSERT_TRUE(append_line(path, "primera"));
    ASSERT_TRUE(append_line(path, "segunda"));
    string read_back;
    ASSERT_TRUE(read_file(path, read_back));
    EXPECT_EQ(read_back, "primera\nsegunda\n");
}

TEST_F(Files, ReadPid) {
    string path = dir + "/nave.pid";
    EXPECT_EQ(read_pid(path), -1);
    ASSERT_TRUE(append_line(path, "4242"));
    EXPECT_EQ(read_pid(path), 4242);
}

TEST(Processes, ProcessAlive) {
    EXPECT_TRUE(process_alive(getpid()));
    EXPECT_FALSE(process_alive(-1));
    EXPECT_FALSE(process_alive(0));

    // Un hijo que ya terminó y fue esperado no existe más.
    pid_t child = fork();
    ASSERT_NE(child, -1);
    if (child == 0) _exit(0);
    EXPECT_TRUE(process_alive(child));  // existe (vivo o zombie) hasta el waitpid
    waitpid(child, nullptr, 0);
    EXPECT_FALSE(process_alive(child));
}

// ----------------------------------------------------------------------------
// Parte 0: ipc/signals.h
// ----------------------------------------------------------------------------

volatile sig_atomic_t g_signal_seen = 0;
void mark_signal(int) { g_signal_seen = 1; }

TEST(Signals, InstallHandlerRunsTheHandler) {
    g_signal_seen = 0;
    ASSERT_TRUE(install_handler(SIGUSR1, mark_signal));
    raise(SIGUSR1);
    EXPECT_EQ(g_signal_seen, 1);
    restore_default(SIGUSR1);
}

TEST(Signals, InstallHandlerUsesSigactionWithoutRestart) {
    ASSERT_TRUE(install_handler(SIGUSR1, mark_signal));
    struct sigaction current {};
    ASSERT_EQ(sigaction(SIGUSR1, nullptr, &current), 0);
    EXPECT_EQ(current.sa_handler, mark_signal);
    EXPECT_FALSE(current.sa_flags & SA_RESTART) << "sin SA_RESTART por defecto";
    ASSERT_TRUE(install_handler(SIGUSR1, mark_signal, SA_RESTART));
    ASSERT_EQ(sigaction(SIGUSR1, nullptr, &current), 0);
    EXPECT_TRUE(current.sa_flags & SA_RESTART) << "si lo piden, tiene que estar";
    restore_default(SIGUSR1);
}

TEST(Signals, SignalInterruptsBlockingRead) {
    // Un read() bloqueante en un pipe vacío, y una alarma que llega a los
    // 100 ms. Sin SA_RESTART, el read() tiene que volver con EINTR.
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    g_signal_seen = 0;
    ASSERT_TRUE(install_handler(SIGALRM, mark_signal));
    struct itimerval timer {};
    timer.it_value.tv_usec = 100 * 1000;
    setitimer(ITIMER_REAL, &timer, nullptr);

    char c;
    ssize_t n = read(fds[0], &c, 1);
    EXPECT_EQ(n, -1);
    EXPECT_EQ(errno, EINTR);
    EXPECT_EQ(g_signal_seen, 1);

    restore_default(SIGALRM);
    close(fds[0]);
    close(fds[1]);
}
