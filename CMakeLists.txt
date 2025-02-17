cmake_minimum_required(VERSION 3.30)
project(xhttp_revised)

set(CMAKE_CXX_STANDARD 23)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g -O1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")


add_executable(xhttp_c

        client/Client.cpp
        logger/Logger.cpp
        includes/crypt.h
        includes/utilx.h
        includes/packet.h
        utils/Utils.cpp
        includes/compressor.h

        )

add_executable(
        test_c

        tests/test.cpp
        logger/Logger.cpp
        includes/crypt.h
        includes/utilx.h
        includes/packet.h
        utils/Utils.cpp
        includes/compressor.h
)

add_executable(xhttp_s

        server/Server.cpp
        logger/Logger.cpp
        includes/crypt.h
        includes/utilx.h
        includes/packet.h
        utils/Utils.cpp
        includes/compressor.h)

target_link_libraries(test_c PRIVATE ZLIB::ZLIB)
target_link_libraries(xhttp_c PRIVATE ZLIB::ZLIB)
target_link_libraries(xhttp_s PRIVATE ZLIB::ZLIB)
target_link_libraries(xhttp_c PRIVATE OpenSSL::Crypto OpenSSL::SSL)
target_link_libraries(test_c PRIVATE OpenSSL::Crypto OpenSSL::SSL)
target_link_libraries(xhttp_s PRIVATE OpenSSL::Crypto OpenSSL::SSL)
