find_package(Qt6 REQUIRED COMPONENTS Test)

function(mdr_test nome)
    add_executable(${nome} ${nome}.cpp)
    target_link_libraries(${nome} PRIVATE mdr_core mdr_fiscal Qt6::Test)
    add_test(NAME ${nome} COMMAND ${nome})
endfunction()

mdr_test(test_core)
