add_library(mdr_fiscal STATIC
    stampantefiscale.h
    simulatore.h  simulatore.cpp
)

target_include_directories(mdr_fiscal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mdr_fiscal PUBLIC mdr_core)
