cmake_minimum_required(VERSION 3.20)
project(FoxDLA VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FoxDLA uses the Win32 API and must be built on Windows.")
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

add_executable(FoxDLA WIN32
    src/mine.cpp
)
target_include_directories(FoxDLA PRIVATE include)
target_link_libraries(FoxDLA PRIVATE ${SLM_WINDOWS_LIBS})

if(MSVC)
    target_compile_options(FoxDLA PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(FoxDLA PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(FoxDLATests
        Test/test.cpp
        src/mine.cpp
    )
    target_include_directories(FoxDLATests PRIVATE include)
    target_compile_definitions(FoxDLATests PRIVATE SYMBOLIC_LINK_MANAGER_NO_ENTRY_POINT=1)
    target_link_libraries(FoxDLATests PRIVATE ${SLM_WINDOWS_LIBS})

    if(MSVC)
        target_compile_options(FoxDLATests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(FoxDLATests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME FoxDLAUnitTests COMMAND FoxDLATests)
endif()
