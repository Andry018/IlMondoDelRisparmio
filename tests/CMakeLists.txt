find_package(Qt6 REQUIRED COMPONENTS Test)

function(mdr_test nome)
    add_executable(${nome} ${nome}.cpp)
    target_link_libraries(${nome} PRIVATE mdr_core mdr_fiscal mdr_locale Qt6::Test)
    add_test(NAME ${nome} COMMAND ${nome})
endfunction()

mdr_test(test_core)
mdr_test(test_locale)
mdr_test(test_custom)

mdr_test(test_sync)
mdr_test(test_aggiornamento)

# Prova delle viste Qt senza finestre a schermo.
function(mdr_test_vista nome libreria)
    add_executable(${nome} ${nome}.cpp)
    target_link_libraries(${nome} PRIVATE ${libreria} Qt6::Test)
    add_test(NAME ${nome} COMMAND ${nome})
    # Su Windows i font di sistema, così le misure delle viste sono quelle del POS (Segoe UI).
    if(WIN32)
        set_tests_properties(${nome} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts")
    else()
        set_tests_properties(${nome} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endfunction()

if(TARGET mdr_cassa)
    mdr_test_vista(test_cassa mdr_cassa)
    mdr_test_vista(test_cassa_server mdr_cassa)   # contro il server di sviluppo, se acceso
endif()
if(TARGET mdr_cassa AND TARGET mdr_gestione)
    # Cassa e gestione contro un server con i dati veri (si salta senza MDR_PROVA_SERVER).
    mdr_test_vista(test_dati_reali "mdr_cassa;mdr_gestione")
    # Installazione completa (gestione, operatori, cassa, prima vendita) contro il server di
    # sviluppo appena ricreato: prova e immagini di docs/GUIDA_INSTALLAZIONE.md.
    mdr_test_vista(test_simulazione_installazione "mdr_cassa;mdr_gestione")
    # Aggiunge una cassiera e CASSA2 al database: dopo gli altri test col server
    set_tests_properties(test_simulazione_installazione PROPERTIES
                         DEPENDS "test_cassa_server;test_gestione_server;test_importazione")
endif()
if(TARGET mdr_gestione)
    mdr_test_vista(test_gestione mdr_gestione)
    # Contro il server di sviluppo: si salta da solo se non è avviato (vedi il file).
    mdr_test_vista(test_gestione_server mdr_gestione)
    mdr_test_vista(test_importazione mdr_gestione)   # parte col server di sviluppo, se acceso
endif()
