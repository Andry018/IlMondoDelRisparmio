find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

# Immagini di banconote e monete (fonti e licenze in risorse/euro/CREDITI.txt).
qt_add_resources(MDR_RISORSE risorse/risorse.qrc)

# Le viste stanno in una libreria per poterle provare nei test (tests/test_desktop.cpp).
add_library(mdr_desktop STATIC
    comune.h
    dimostrativi.h       dimostrativi.cpp
    dialogocontanti.h    dialogocontanti.cpp
    impostazioni.h       impostazioni.cpp
    vistacassa.h         vistacassa.cpp
    vistavendite.h       vistavendite.cpp
    vistaarticoli.h      vistaarticoli.cpp
    vistaimpostazioni.h  vistaimpostazioni.cpp
    finestraprincipale.h finestraprincipale.cpp
    ${MDR_RISORSE}
)
target_include_directories(mdr_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mdr_desktop PUBLIC mdr_core mdr_fiscal mdr_locale Qt6::Widgets)

add_executable(mdr-cassa WIN32 main.cpp)
target_link_libraries(mdr-cassa PRIVATE mdr_desktop)
