cmake_minimum_required(VERSION 3.28)
project(KairoEngineCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(KAIRO_ENGINE_CORE_BUILD_TESTS "Build KairoEngineCore tests" ${PROJECT_IS_TOP_LEVEL})

# Immutable revisions used only outside a local workspace checkout. They track
# merged component commits validated by the KairoGameEngine superbuild.
set(KAIRO_ENGINE_CORE_MATH_REVISION e12778734a8e8ffe060255a9c03683ad6c4b1a92)
set(KAIRO_ENGINE_CORE_ASSETS_REVISION agent/phases8-11-authoring-completion)
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
    set(KAIRO_ASSETS_SOURCE_DIR "" CACHE PATH "Path to local KairoAssets")
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
    TextValidation.cppm TextFormat.cppm Entity.cppm Components.cppm RuntimeComponents.cppm ProjectDescriptor.cppm
    Scene.cppm SceneSerialization.cppm RuntimeSceneCommands.cppm RuntimeWorld.cppm
    NativeGameplay.cppm NativeGameplayManifest.cppm ProductionSystems.cppm
    CoreReflection.cppm Event.cppm Time.cppm Input.cppm InputMap.cppm
    LogicState.cppm LogicBytecode.cppm LogicArtifact.cppm GameplayVM.cppm Layer.cppm
    Platform.cppm Logger.cppm CoreDiagnostics.cppm JobSystem.cppm Application.cppm KairoEngineCore.cppm)
target_link_libraries(KairoEngineCore PUBLIC KairoMath KairoReflection KairoAssets)
add_library(Kairo::EngineCore ALIAS KairoEngineCore)

if(KAIRO_ENGINE_CORE_BUILD_TESTS)
    include(CTest)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(KairoEngineCoreTests
        tests/EngineCoreTests.cpp
        tests/RenderingComponentTests.cpp
        tests/DeterministicBytecodeTests.cpp
        tests/LogicStateTests.cpp
        tests/RuntimeSceneCommandsTests.cpp
        tests/NativeGameplayTests.cpp
        tests/NativeGameplayManifestTests.cpp
        tests/ProductionSystemsTests.cpp
        tests/PlatformTests.cpp)
    target_link_libraries(KairoEngineCoreTests PRIVATE KairoEngineCore Catch2::Catch2WithMain)
    add_executable(KairoEngineCorePhase3Tests tests/Phase3RuntimeTests.cpp)
    target_link_libraries(KairoEngineCorePhase3Tests PRIVATE KairoEngineCore Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME KairoEngineCoreTests COMMAND KairoEngineCoreTests)
    add_test(NAME KairoEngineCorePhase3Tests COMMAND KairoEngineCorePhase3Tests)
endif()
