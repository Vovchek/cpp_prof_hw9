cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)

set(PATCH_VERSION "1" CACHE INTERNAL "Patch version")
set(PROJECT_VESRION 0.0.${PATCH_VERSION})

project(async VERSION ${PROJECT_VESRION})

add_library(${PROJECT_NAME} SHARED
    homework/async.cpp
    homework/async.h
)

add_executable(${PROJECT_NAME}_cli
    homework/main.cpp
)

set_target_properties(async_cli PROPERTIES    # target PROPERTIES prop1 value1  ...
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_compile_options(async PRIVATE
    -Wall -Wextra -pedantic -Werror
)

target_link_libraries(async_cli async)

install(TARGETS async async_cli RUNTIME DESTINATION bin)

set(CPACK_GENERATOR DEB)

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

set(CPACK_PACKAGE_CONTACT vovchek64@inbox.ru)

include(CPack)
