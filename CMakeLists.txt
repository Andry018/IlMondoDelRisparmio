cmake_minimum_required(VERSION 3.21)

project(MondoDelRisparmio
    VERSION 0.1.0
    DESCRIPTION "Gestionale retail: cassa, magazzino, fatturazione"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Il server è Supabase self-hosted (vedi supabase/ e infra/), non c'è un server C++.
# Client desktop: da attivare quando il modulo sarà presente.
option(MDR_BUILD_DESKTOP "Compila il client desktop (mdr-cassa)" OFF)
option(MDR_BUILD_TESTS   "Compila i test"                         ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Network)

add_subdirectory(src/core)
add_subdirectory(src/fiscal)

if(MDR_BUILD_DESKTOP)
    add_subdirectory(src/desktop)
endif()

if(MDR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
