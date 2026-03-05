cmake_minimum_required(VERSION 3.20)
project(Pasta VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to shared unless explicitly set (must precede add_library)
if(NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS ON)
endif()

# Library (shared by default, static via -DBUILD_SHARED_LIBS=OFF)
add_library(pasta
    src/main/c/pasta_lexer.c
    src/main/c/pasta_parser.c
    src/main/c/pasta_value.c
    src/main/c/pasta_writer.c
)

target_include_directories(pasta
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/main/h>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/main/c
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(pasta PRIVATE PASTA_BUILDING)
    set_target_properties(pasta PROPERTIES
        VERSION   ${PROJECT_VERSION}
        SOVERSION 0
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
else()
    # Static build: PASTA_API expands to nothing (no dllimport/export)
    target_compile_definitions(pasta PUBLIC PASTA_STATIC)
    set_target_properties(pasta PROPERTIES
        VERSION   ${PROJECT_VERSION}
    )
endif()

if(WIN32)
    set_target_properties(pasta PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
endif()

# Place test exe next to the DLL so it finds it at runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Test executable
option(PASTA_BUILD_TESTS "Build test executable" ON)
if(PASTA_BUILD_TESTS)
    add_executable(pasta_test src/test/c/pasta_test.c)
    target_link_libraries(pasta_test PRIVATE pasta)
    target_compile_definitions(pasta_test PRIVATE
        PASTA_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/src/test/resources"
    )
endif()

# Install rules
include(GNUInstallDirs)
install(TARGETS pasta
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES src/main/h/pasta.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
