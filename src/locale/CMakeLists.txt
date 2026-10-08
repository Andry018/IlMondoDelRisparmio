add_library(mdr_locale STATIC
    archiviolocale.h archiviolocale.cpp
    payloadvendita.h payloadvendita.cpp
    operazionicassa.h operazionicassa.cpp
    sincronizzatore.h sincronizzatore.cpp
)

target_include_directories(mdr_locale PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mdr_locale PUBLIC mdr_core mdr_fiscal Qt6::Sql Qt6::Network)
