cmake_minimum_required(VERSION 3.21)

project(MondoDelRisparmio
    VERSION 0.4.0
    DESCRIPTION "Gestionale retail: cassa, magazzino, fatturazione"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Il server è Supabase self-hosted (vedi supabase/ e infra/), non c'è un server C++.
# Client desktop Qt Widgets: due programmi separati, installati entrambi su ogni PC.
#   mdr-cassa     vendita al banco col registratore telematico
#   mdr-gestione  anagrafiche e amministrazione
option(MDR_BUILD_DESKTOP "Compila i client desktop (mdr-cassa e mdr-gestione)" ON)
option(MDR_BUILD_TESTS   "Compila i test"                         ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Network)

add_subdirectory(src/core)
add_subdirectory(src/fiscal)
add_subdirectory(src/locale)

# Prova a mano sul registratore Custom vero (in addestramento): non è in ctest, vedi il file.
# Si compila sempre (anche senza test) perché va nel pacchetto di installazione: la prova si fa
# dal PC del negozio collegato al registratore.
add_executable(mdr-prova-rt tests/prova_rt_vero.cpp)
target_link_libraries(mdr-prova-rt PRIVATE mdr_core mdr_fiscal)

if(MDR_BUILD_DESKTOP)
    add_subdirectory(src/third_party/qrcodegen)
    add_subdirectory(src/ui)
    add_subdirectory(src/cassa)
    add_subdirectory(src/gestione)
endif()

if(MDR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
