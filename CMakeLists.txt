cmake_minimum_required(VERSION 3.28)
project(KairoEngineCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(KAIRO_ENGINE_CORE_BUILD_TESTS "Build KairoEngineCore tests" ${PROJECT_IS_TOP_LEVEL})

# Immutable revisions used only outside a local workspace checkout. They track
# merged component commits validated by the KairoGameEngine superbuild.
set(KAIRO_ENGINE_CORE_MATH_REVISION 06ef6c59f88541c80ff5867a1e7557e10b5cca77)
set(KAIRO_ENGINE_CORE_ASSETS_REVISION 233a24954adbee34d746d91848786734b827a4ce)
set(KAIRO_ENGINE_CORE_REFLECTION_REVISION 1e27101b0e3fcac79aa51bf67b888c8783ad3b3f)

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
            GIT_TAG ${KAIRO_ENGINE_CORE_REFLECTION_REVISION})
        FetchContent_MakeAvailable(KairoReflection)
    endif()
endif()

if(NOT TARGET KairoMath)
    set(KAIRO_MATH_SOURCE_DIR "" CACHE PATH "Path to local KairoMath")
    if(KAIRO_MATH_SOURCE_DIR AND EXISTS "${KAIRO_MATH_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KAIRO_MATH_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoMath")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../KairoMath/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../KairoMath" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoMath")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Foundation/KairoMath/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Foundation/KairoMath" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoMath")
    else()
        include(FetchContent)
        FetchContent_Declare(KairoMath
            GIT_REPOSITORY https://github.com/swayam8624/KairoMath.git
            GIT_TAG ${KAIRO_ENGINE_CORE_MATH_REVISION})
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
            GIT_TAG ${KAIRO_ENGINE_CORE_ASSETS_REVISION})
        FetchContent_MakeAvailable(KairoAssets)
    endif()
endif()

add_library(KairoEngineCore)
target_sources(KairoEngineCore PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    TextValidation.cppm TextFormat.cppm Entity.cppm Components.cppm RuntimeComponents.cppm ProjectDescriptor.cppm
    Scene.cppm SceneSerialization.cppm SceneSerializationV5.cppm SceneComposition.cppm RuntimeSceneCommands.cppm RuntimeWorld.cppm WorldStreaming.cppm AnimationRuntime.cppm AnimationController.cppm AnimationRootMotion.cppm GltfSkinningRuntime.cppm AudioRuntime.cppm AudioSceneComponents.cppm AudioScenePersistence.cppm
    NativeGameplay.cppm NativeGameplayManifest.cppm ProductionSystems.cppm ProductionSystemsManifest.cppm ProductionRuntime.cppm ShippingRuntime.cppm
    CoreReflection.cppm Event.cppm Time.cppm Input.cppm InputMap.cppm
    LogicState.cppm LogicBytecode.cppm LogicArtifact.cppm GameplayVM.cppm SaveGame.cppm Layer.cppm
    Platform.cppm Logger.cppm CoreDiagnostics.cppm JobSystem.cppm Application.cppm KairoEngineCore.cppm)
target_link_libraries(KairoEngineCore PUBLIC KairoMath KairoReflection KairoAssets)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # PropertyMetadata deliberately keeps appended fields defaulted so older
    # aggregate initializers remain source-compatible. Clang 19+ distinguishes
    # designated-field diagnostics; older Clang versions only know the general
    # missing-field warning.
    set(_kairo_reflection_warning_options -Wno-missing-field-initializers)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19)
        list(APPEND _kairo_reflection_warning_options
            -Wno-missing-designated-field-initializers)
    endif()
    set_source_files_properties(CoreReflection.cppm PROPERTIES
        COMPILE_OPTIONS "${_kairo_reflection_warning_options}")
endif()
set_property(TARGET KairoEngineCore PROPERTY COMPILE_WARNING_AS_ERROR ON)
add_library(Kairo::EngineCore ALIAS KairoEngineCore)

if(KAIRO_ENGINE_CORE_BUILD_TESTS)
    include(CTest)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(KairoEngineCoreTests
        tests/EngineCoreTests.cpp
        tests/RenderingComponentTests.cpp
        tests/ReflectionV2IntegrationTests.cpp
        tests/DeterministicBytecodeTests.cpp
        tests/LogicArtifactPayloadBoundaryTests.cpp
        tests/LogicStateTests.cpp
        tests/RuntimeSceneCommandsTests.cpp
        tests/WorldStreamingTests.cpp
        tests/WorldStreamingScaleTests.cpp
        tests/SceneCompositionTests.cpp
        tests/SceneAudioSerializationV5Tests.cpp
        tests/AnimationRuntimeTests.cpp
        tests/AnimationControllerTests.cpp
        tests/AnimationRootMotionTests.cpp
        tests/AudioRuntimeTests.cpp
        tests/AudioSceneComponentTests.cpp
        tests/AudioScenePersistenceTests.cpp
        tests/SaveGameTests.cpp
        tests/NativeGameplayTests.cpp
        tests/NativeGameplayManifestTests.cpp
        tests/ProductionSystemsTests.cpp
        tests/ProductionRuntimeTests.cpp
        tests/ShippingRuntimeTests.cpp
        tests/PlatformTests.cpp
        tests/ProjectDescriptorTests.cpp)
    target_link_libraries(KairoEngineCoreTests PRIVATE KairoEngineCore Catch2::Catch2WithMain)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 23)
        target_compile_options(KairoEngineCoreTests PRIVATE
            -Wno-c2y-extensions
            -Wno-missing-field-initializers
            -Wno-missing-designated-field-initializers)
    endif()
    set_property(TARGET KairoEngineCoreTests PROPERTY COMPILE_WARNING_AS_ERROR ON)
    add_executable(KairoEngineCorePhase3Tests tests/Phase3RuntimeTests.cpp)
    target_link_libraries(KairoEngineCorePhase3Tests PRIVATE KairoEngineCore Catch2::Catch2WithMain)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 23)
        target_compile_options(KairoEngineCorePhase3Tests PRIVATE
            -Wno-c2y-extensions
            -Wno-missing-field-initializers
            -Wno-missing-designated-field-initializers)
    endif()
    set_property(TARGET KairoEngineCorePhase3Tests PROPERTY COMPILE_WARNING_AS_ERROR ON)
    enable_testing()
    add_test(NAME KairoEngineCoreTests COMMAND KairoEngineCoreTests)
    add_test(NAME KairoEngineCorePhase3Tests COMMAND KairoEngineCorePhase3Tests)
endif()
