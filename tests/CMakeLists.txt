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
    set_tests_properties(${nome} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

if(TARGET mdr_cassa)
    mdr_test_vista(test_cassa mdr_cassa)
    mdr_test_vista(test_cassa_server mdr_cassa)   # contro il server di sviluppo, se acceso
endif()
if(TARGET mdr_gestione)
    mdr_test_vista(test_gestione mdr_gestione)
    # Contro il server di sviluppo: si salta da solo se non è avviato (vedi il file).
    mdr_test_vista(test_gestione_server mdr_gestione)
    mdr_test_vista(test_importazione mdr_gestione)   # parte col server di sviluppo, se acceso
endif()
