# Le viste stanno in una libreria per poterle provare nei test (tests/test_gestione.cpp).
add_library(mdr_gestione STATIC
    vistaarticoli.h      vistaarticoli.cpp
    finestragestione.h   finestragestione.cpp
)
target_include_directories(mdr_gestione PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdr_gestione PUBLIC mdr_ui)

add_executable(mdr-gestione WIN32 main.cpp)
target_link_libraries(mdr-gestione PRIVATE mdr_gestione)
