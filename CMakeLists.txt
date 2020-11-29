cmake_minimum_required(VERSION 3.7...3.19)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(ShaderCompile CXX)

option(RE2_BUILD_TESTING "" OFF)

add_subdirectory(shared/re2)
add_subdirectory(shared/gsl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SRC
    ShaderCompile/cfgprocessor.cpp
    ShaderCompile/d3dxfxc.cpp
    ShaderCompile/ShaderCompile.cpp
    ShaderCompile/shaderparser.cpp
    ShaderCompile/utlbuffer.cpp
    ShaderCompile/utlsymbol.cpp
    )

add_executable(ShaderCompile ${SRC})
target_link_libraries(ShaderCompile re2::re2 Microsoft.GSL::GSL)
include_directories(ShaderCompile/include shared/re2)

set_property(TARGET ShaderCompile PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET re2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(ShaderCompile PRIVATE _ITERATOR_DEBUG_LEVEL=0)
target_compile_definitions(re2 PRIVATE _ITERATOR_DEBUG_LEVEL=0)