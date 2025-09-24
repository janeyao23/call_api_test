
cmake_minimum_required(VERSION 3.10)
project(get_pair_his CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(ZLIB REQUIRED)

pkg_check_modules(LWS REQUIRED libwebsockets)

add_executable(call_api_test main.cpp call_api_demo.cpp)

target_include_directories(get_pair_his PRIVATE ${LWS_INCLUDE_DIRS})
target_compile_options(get_pair_his PRIVATE ${LWS_CFLAGS_OTHER})
target_link_libraries(get_pair_his PRIVATE
    ${LWS_LIBRARIES}
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)
