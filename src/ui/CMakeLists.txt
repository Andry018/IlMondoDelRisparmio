find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

# Parte comune di mdr-cassa e mdr-gestione: impostazioni della postazione, finestra con
# sincronizzazione e scheda Impostazioni, piccoli aiuti per le viste.
add_library(mdr_ui STATIC
    comune.h
    dimostrativi.h       dimostrativi.cpp
    impostazioni.h       impostazioni.cpp
    vistaimpostazioni.h  vistaimpostazioni.cpp
    finestrabase.h       finestrabase.cpp
    installazioneaggiornamento.h installazioneaggiornamento.cpp
    italiano.h           italiano.cpp
)
target_include_directories(mdr_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mdr_ui PUBLIC mdr_core mdr_locale Qt6::Widgets)
# Versione dei programmi (project() in CMakeLists.txt): la usa l'aggiornamento automatico.
target_compile_definitions(mdr_ui PUBLIC MDR_VERSIONE="${PROJECT_VERSION}")
