# QR Code generator library di Project Nayuki, v1.8.0 (licenza MIT, avviso nei file).
# https://www.nayuki.io/page/qr-code-generator-library — file copiati senza modifiche.
# Serve alla gestione per il QR di configurazione dei palmari.
add_library(qrcodegen STATIC qrcodegen.hpp qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
