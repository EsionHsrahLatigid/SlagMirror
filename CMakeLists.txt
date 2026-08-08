cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(SlagMirror VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(SlagMirror VERSION 0.1.0 LANGUAGES C CXX)
endif()

option(SLAGMIRROR_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(SLAGMIRROR_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(SLAGMIRROR_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(SLAGMIRROR_CREATE_AU OFF)
if(APPLE)
    set(SLAGMIRROR_CREATE_AU ON)
endif()
set(SLAGMIRROR_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(slagmirror_engine STATIC
    source/SlagMirrorEngine.cpp)
target_include_directories(slagmirror_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(slagmirror_engine PUBLIC cxx_std_20)
set_target_properties(slagmirror_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(slagmirror_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SLAGMIRROR_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(slagmirror_engine_tests
        tests/SlagMirrorEngineTests.cpp)
    target_link_libraries(slagmirror_engine_tests PRIVATE slagmirror_engine)
    target_compile_features(slagmirror_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(slagmirror_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(slagmirror_engine_tests)
    add_test(NAME slagmirror_engine_tests COMMAND slagmirror_engine_tests)
endif()

if(SLAGMIRROR_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${SLAGMIRROR_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${SLAGMIRROR_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
    else()
        include(FetchContent)
        FetchContent_Declare(yup
            GIT_REPOSITORY https://github.com/kunitoki/yup.git
            GIT_TAG 9a1c9bc699b6a714f6f52486462d98a140c8bf95)
        FetchContent_MakeAvailable(yup)
    endif()

    # YUP's iconutil-based helper rejects its generated iconset on current
    # macOS. Keep the compatibility fix project-local until upstream changes.
    if(APPLE)
        include(cmake/YupMacOSIconWorkaround.cmake)
    endif()

    yup_audio_plugin(
        TARGET_NAME slagmirror
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "SlagMirror"
        TARGET_APP_ID "audio.2bit.slagmirror"
        TARGET_APP_NAMESPACE "audio.2bit"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "audio.2bit.SlagMirror"
        PLUGIN_NAME "SlagMirror"
        PLUGIN_VENDOR "2bit"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "Stereo sample-domain mirror folding and reflective scatter effect"
        PLUGIN_URL "https://github.com/2bbb/SlagMirror"
        PLUGIN_IS_SYNTH OFF
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${SLAGMIRROR_CREATE_AU}
        PLUGIN_AU_SUBTYPE "SlMr"
        PLUGIN_AU_MANUFACTURER "2Bit"
        PLUGIN_AU_SANDBOX_SAFE ON
        PLUGIN_CREATE_AUv3 OFF
        PLUGIN_CREATE_LV2 OFF
        PLUGIN_CREATE_AAX OFF
        PLUGIN_CREATE_STANDALONE ON
        PLUGIN_COPY_AFTER_BUILD OFF
        MODULES
            yup::yup_gui
            yup::yup_audio_processors)

    target_sources(slagmirror_shared INTERFACE
        source/ParameterGridEditor.cpp
        source/ParameterGridEditor.h
        source/ProductState.h
        source/SlagMirrorPlugin.cpp
        source/SlagMirrorPlugin.h)
    target_link_libraries(slagmirror_shared INTERFACE slagmirror_engine)
    target_include_directories(slagmirror_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    if(SLAGMIRROR_BUILD_TESTS)
        add_executable(slagmirror_plugin_bridge_tests
            tests/SlagMirrorPluginBridgeTests.cpp
            source/SlagMirrorPlugin.cpp)
        target_link_libraries(slagmirror_plugin_bridge_tests PRIVATE slagmirror_engine yup::yup_audio_processors)
        target_include_directories(slagmirror_plugin_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(slagmirror_plugin_bridge_tests PRIVATE SLAGMIRROR_HEADLESS_TEST=1)
        target_compile_features(slagmirror_plugin_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(slagmirror_plugin_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(slagmirror_plugin_bridge_tests)
        add_test(NAME slagmirror_plugin_bridge_tests COMMAND slagmirror_plugin_bridge_tests)

        add_executable(slagmirror_hosted_negative_bridge_tests
            tests/SlagMirrorPluginBridgeTests.cpp
            source/SlagMirrorPlugin.cpp)
        target_link_libraries(slagmirror_hosted_negative_bridge_tests PRIVATE slagmirror_engine yup::yup_audio_processors)
        target_include_directories(slagmirror_hosted_negative_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(slagmirror_hosted_negative_bridge_tests PRIVATE
            SLAGMIRROR_HEADLESS_TEST=1
            SLAGMIRROR_ENABLE_STANDALONE_AUDITION=1)
        target_compile_features(slagmirror_hosted_negative_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(slagmirror_hosted_negative_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(slagmirror_hosted_negative_bridge_tests)
        add_test(NAME slagmirror_hosted_negative_bridge_tests COMMAND slagmirror_hosted_negative_bridge_tests)

        add_executable(slagmirror_standalone_bridge_tests
            tests/SlagMirrorPluginBridgeTests.cpp
            source/SlagMirrorPlugin.cpp)
        target_link_libraries(slagmirror_standalone_bridge_tests PRIVATE slagmirror_engine yup::yup_audio_processors)
        target_include_directories(slagmirror_standalone_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(slagmirror_standalone_bridge_tests PRIVATE
            SLAGMIRROR_HEADLESS_TEST=1
            YUP_AUDIO_PLUGIN_ENABLE_STANDALONE=1)
        target_compile_features(slagmirror_standalone_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(slagmirror_standalone_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(slagmirror_standalone_bridge_tests)
        add_test(NAME slagmirror_standalone_bridge_tests COMMAND slagmirror_standalone_bridge_tests)
    endif()

    set(slagmirror_release_bundle_targets
        slagmirror_standalone_plugin
        slagmirror_vst3_plugin)
    if(APPLE)
        list(APPEND slagmirror_release_bundle_targets slagmirror_au_plugin)
    endif()

    add_custom_target(slagmirror_release_bundles
        DEPENDS ${slagmirror_release_bundle_targets})

    if(APPLE)
        set_target_properties(slagmirror_standalone_plugin PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "audio.2bit.slagmirror"
            MACOSX_BUNDLE_BUNDLE_NAME "SlagMirror"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "audio.2bit.slagmirror")
    endif()

    if(APPLE AND SLAGMIRROR_ADHOC_SIGN)
        find_program(SLAGMIRROR_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET slagmirror_vst3_plugin POST_BUILD
            COMMAND "${SLAGMIRROR_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:slagmirror_vst3_plugin>"
            COMMENT "Ad-hoc signing SlagMirror VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET slagmirror_standalone_plugin POST_BUILD
            COMMAND "${SLAGMIRROR_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:slagmirror_standalone_plugin>"
            COMMENT "Ad-hoc signing SlagMirror standalone app for local launch"
            VERBATIM)
    endif()
endif()
