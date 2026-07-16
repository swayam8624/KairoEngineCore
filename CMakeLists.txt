cmake_minimum_required(VERSION 3.28)
project(KairoEngineCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(KAIRO_ENGINE_CORE_BUILD_TESTS "Build KairoEngineCore tests" ${PROJECT_IS_TOP_LEVEL})

# Immutable revisions used only outside a local workspace checkout. They track
# the component commits validated by the KairoGameEngine superbuild.
set(KAIRO_ENGINE_CORE_MATH_REVISION d77c98d7401f63b697c1486ebf59a1fae74c6490)
set(KAIRO_ENGINE_CORE_ASSETS_REVISION 306a715094ff21707cba209e38eecb282bbb9f1c)
set(KAIRO_ENGINE_CORE_REFLECTION_REVISION 7aaa24a4767433ad8911e0accbb2a480e607ded8)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

if(NOT TARGET KairoReflection)
    set(KAIRO_REFLECTION_SOURCE_DIR "" CACHE PATH "Path to a local KairoReflection checkout")
    if(KAIRO_REFLECTION_SOURCE_DIR AND EXISTS "${KAIRO_REFLECTION_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KAIRO_REFLECTION_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoReflection")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../KairoReflection/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../KairoReflection" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoReflection")
    else()
        include(FetchContent)
        FetchContent_Declare(KairoReflection
            GIT_REPOSITORY https://github.com/swayam8624/KairoReflection.git
            GIT_TAG ${KAIRO_ENGINE_CORE_REFLECTION_REVISION}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(KairoReflection)
    endif()
endif()

if(NOT TARGET KairoMath)
    set(KAIRO_MATH_SOURCE_DIR "" CACHE PATH "Path to local KairoMath")
    if(KAIRO_MATH_SOURCE_DIR AND EXISTS "${KAIRO_MATH_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KAIRO_MATH_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoMath")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Foundation/KairoMath/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Foundation/KairoMath" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoMath")
    else()
        include(FetchContent)
        FetchContent_Declare(KairoMath
            GIT_REPOSITORY https://github.com/swayam8624/KairoMath.git
            GIT_TAG ${KAIRO_ENGINE_CORE_MATH_REVISION}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(KairoMath)
    endif()
endif()

if(NOT TARGET KairoAssets)
    set(KAIRO_ASSETS_SOURCE_DIR "" CACHE PATH "Path to a local KairoAssets checkout")
    if(KAIRO_ASSETS_SOURCE_DIR AND EXISTS "${KAIRO_ASSETS_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KAIRO_ASSETS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoAssets")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../KairoAssets/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../KairoAssets" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoAssets")
    else()
        include(FetchContent)
        FetchContent_Declare(KairoAssets
            GIT_REPOSITORY https://github.com/swayam8624/KairoAssets.git
            GIT_TAG ${KAIRO_ENGINE_CORE_ASSETS_REVISION}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(KairoAssets)
    endif()
endif()

add_library(KairoEngineCore)
target_sources(KairoEngineCore PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    TextValidation.cppm TextFormat.cppm ProjectDescriptor.cppm Entity.cppm Components.cppm RuntimeComponents.cppm Scene.cppm SceneSerialization.cppm CoreReflection.cppm Time.cppm Event.cppm Input.cppm Layer.cppm Logger.cppm CoreDiagnostics.cppm JobSystem.cppm Application.cppm KairoEngineCore.cppm)
target_link_libraries(KairoEngineCore PUBLIC KairoMath KairoAssets KairoReflection)

if(KAIRO_ENGINE_CORE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(KairoEngineCoreTests tests/EngineCoreTests.cpp)
    target_link_libraries(KairoEngineCoreTests PRIVATE KairoEngineCore Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME KairoEngineCoreTests COMMAND KairoEngineCoreTests)
endif()
