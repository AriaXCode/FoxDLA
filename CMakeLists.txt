cmake_minimum_required(VERSION 3.20)
project(SymbolicLinkManager VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Symbolic Link Manager uses the Win32 API and must be built on Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SLM_WINDOWS_LIBS
    user32
    gdi32
    comctl32
    comdlg32
    ole32
    shell32
    advapi32
    uuid
)

add_executable(SymbolicLinkManager WIN32
    src/mine.cpp
)
target_include_directories(SymbolicLinkManager PRIVATE include)
target_link_libraries(SymbolicLinkManager PRIVATE ${SLM_WINDOWS_LIBS})

if(MSVC)
    target_compile_options(SymbolicLinkManager PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(SymbolicLinkManager PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(SymbolicLinkManagerTests
        Test/test.cpp
        src/mine.cpp
    )
    target_include_directories(SymbolicLinkManagerTests PRIVATE include)
    target_compile_definitions(SymbolicLinkManagerTests PRIVATE SYMBOLIC_LINK_MANAGER_NO_ENTRY_POINT=1)
    target_link_libraries(SymbolicLinkManagerTests PRIVATE ${SLM_WINDOWS_LIBS})

    if(MSVC)
        target_compile_options(SymbolicLinkManagerTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(SymbolicLinkManagerTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME SymbolicLinkManagerUnitTests COMMAND SymbolicLinkManagerTests)
endif()
