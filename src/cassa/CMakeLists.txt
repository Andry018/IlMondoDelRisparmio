# Immagini di banconote e monete (fonti e licenze in risorse/euro/CREDITI.txt).
qt_add_resources(MDR_RISORSE_CASSA risorse/risorse.qrc)

# Le viste stanno in una libreria per poterle provare nei test (tests/test_cassa.cpp).
add_library(mdr_cassa STATIC
    dialogocontanti.h    dialogocontanti.cpp
    dialogooperatore.h   dialogooperatore.cpp
    aspettocassa.h       aspettocassa.cpp
    vistaaspetto.h       vistaaspetto.cpp
    schermocliente.h     schermocliente.cpp
    vistacassa.h         vistacassa.cpp
    vistavendite.h       vistavendite.cpp
    finestracassa.h      finestracassa.cpp
    ${MDR_RISORSE_CASSA}
)
target_include_directories(mdr_cassa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdr_cassa PUBLIC mdr_ui mdr_fiscal)

# Logo (risorse/icona.svg, da cui icona.png e icona.ico): finestra ed eseguibile
qt_add_resources(MDR_ICONA_CASSA risorse/icona.qrc)
add_executable(mdr-cassa WIN32 main.cpp ${MDR_ICONA_CASSA} risorse/mdr-cassa.rc)
target_link_libraries(mdr-cassa PRIVATE mdr_cassa)
