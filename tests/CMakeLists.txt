find_package(Qt6 REQUIRED COMPONENTS Test)

function(mdr_test nome)
    add_executable(${nome} ${nome}.cpp)
    target_link_libraries(${nome} PRIVATE mdr_core mdr_fiscal mdr_locale Qt6::Test)
    add_test(NAME ${nome} COMMAND ${nome})
endfunction()

mdr_test(test_core)
mdr_test(test_locale)

mdr_test(test_sync)
mdr_test(test_aggiornamento)

# Prova delle viste Qt senza finestre a schermo.
if(TARGET mdr_desktop)
    add_executable(test_desktop test_desktop.cpp)
    target_link_libraries(test_desktop PRIVATE mdr_desktop Qt6::Test)
    add_test(NAME test_desktop COMMAND test_desktop)
    set_tests_properties(test_desktop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
