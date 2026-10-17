# Le viste stanno in una libreria per poterle provare nei test (tests/test_gestione.cpp).
add_library(mdr_gestione STATIC
    vistaarticoli.h      vistaarticoli.cpp
    finestragestione.h   finestragestione.cpp
    tabellaserver.h      tabellaserver.cpp
    vistaserver.h        vistaserver.cpp
    vistareport.h        vistareport.cpp
    vistacontrolli.h     vistacontrolli.cpp
    vistamagazzino.h     vistamagazzino.cpp
    vistaarticoliserver.h vistaarticoliserver.cpp
    vistaclienti.h       vistaclienti.cpp
    vistafatture.h       vistafatture.cpp
    vistaamministrazione.h vistaamministrazione.cpp
    filetabella.h        filetabella.cpp
    importazione.h       importazione.cpp
    importatore.h        importatore.cpp
    dialogoimportazione.h dialogoimportazione.cpp
    vistapalmare.h       vistapalmare.cpp
)
target_include_directories(mdr_gestione PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(ZLIB REQUIRED)   # per leggere i file Excel (.xlsx sono zip)
target_link_libraries(mdr_gestione PUBLIC mdr_ui PRIVATE ZLIB::ZLIB qrcodegen)

add_executable(mdr-gestione WIN32 main.cpp)
target_link_libraries(mdr-gestione PRIVATE mdr_gestione)
