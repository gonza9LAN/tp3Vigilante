cmake_minimum_required(VERSION 3.14)
project(tp3 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Los warnings se aplican solo a nuestro código, no a googletest.
add_compile_options(-Wall -Wextra)

# La simulación. Se compila una vez y la usan los tres programas y los tests.
add_library(sim STATIC sim/subsystem.cpp)
target_include_directories(sim PUBLIC ${CMAKE_SOURCE_DIR})

# Los tres programas del TP.
add_executable(nave nave.cpp)
target_link_libraries(nave PRIVATE sim)

add_executable(subsistema subsistema_main.cpp)
target_link_libraries(subsistema PRIVATE sim)

add_executable(tierra tierra.cpp)
target_link_libraries(tierra PRIVATE sim)

# El ejemplo de fork + exec + dup2.
add_executable(ejemplo_hijo ejemplo/hijo.cpp)
target_include_directories(ejemplo_hijo PRIVATE ${CMAKE_SOURCE_DIR})

add_executable(ejemplo_lanzar ejemplo/lanzar.cpp)
target_include_directories(ejemplo_lanzar PRIVATE ${CMAKE_SOURCE_DIR})

# Tests del código entregado.
add_executable(tp_tests tests/tests.cpp)
target_link_libraries(tp_tests PRIVATE sim GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tp_tests)
