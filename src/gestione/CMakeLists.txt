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
    schedaarticolo.h     schedaarticolo.cpp
    dialogoprezzi.h      dialogoprezzi.cpp
    vistacruscotto.h     vistacruscotto.cpp
    etichette.h          etichette.cpp
    vistaetichette.h     vistaetichette.cpp
    dialogoordine.h      dialogoordine.cpp
    dialogocontrollodati.h dialogocontrollodati.cpp
)
target_include_directories(mdr_gestione PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Qt6 REQUIRED COMPONENTS PrintSupport)   # etichette prezzi
find_package(ZLIB REQUIRED)   # per leggere i file Excel (.xlsx sono zip)
target_link_libraries(mdr_gestione PUBLIC mdr_ui Qt6::PrintSupport PRIVATE ZLIB::ZLIB qrcodegen)

# Logo (risorse/icona.svg, da cui icona.png e icona.ico): finestra ed eseguibile
qt_add_resources(MDR_ICONA_GESTIONE risorse/icona.qrc)
add_executable(mdr-gestione WIN32 main.cpp ${MDR_ICONA_GESTIONE} risorse/mdr-gestione.rc)
target_link_libraries(mdr-gestione PRIVATE mdr_gestione)
