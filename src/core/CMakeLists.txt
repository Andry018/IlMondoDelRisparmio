add_library(mdr_core STATIC
    money.h        money.cpp
    quantita.h     quantita.cpp
    iva.h          iva.cpp
    barcode.h      barcode.cpp
    modelli.h      modelli.cpp
    carrello.h     carrello.cpp
    codicefiscale.h codicefiscale.cpp
    promozioni.h   promozioni.cpp
)

target_include_directories(mdr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mdr_core PUBLIC Qt6::Core)
