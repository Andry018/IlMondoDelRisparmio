# Immagini di banconote e monete (fonti e licenze in risorse/euro/CREDITI.txt).
qt_add_resources(MDR_RISORSE_CASSA risorse/risorse.qrc)

# Le viste stanno in una libreria per poterle provare nei test (tests/test_cassa.cpp).
add_library(mdr_cassa STATIC
    dialogocontanti.h    dialogocontanti.cpp
    dialogooperatore.h   dialogooperatore.cpp
    vistacassa.h         vistacassa.cpp
    vistavendite.h       vistavendite.cpp
    finestracassa.h      finestracassa.cpp
    ${MDR_RISORSE_CASSA}
)
target_include_directories(mdr_cassa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdr_cassa PUBLIC mdr_ui mdr_fiscal)

add_executable(mdr-cassa WIN32 main.cpp)
target_link_libraries(mdr-cassa PRIVATE mdr_cassa)
