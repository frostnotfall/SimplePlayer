cmake_minimum_required(VERSION 3.25)
project(SimplePlayer VERSION 0.1.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows x64 with MSVC is required")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# This MSVC installation has only Chinese diagnostics. Override the incorrectly
# decoded cached detection so Ninja tracks header changes instead of printing them.
get_filename_component(MSVC_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(EXISTS "${MSVC_COMPILER_DIRECTORY}/2052/clui.dll" AND NOT EXISTS "${MSVC_COMPILER_DIRECTORY}/1033/clui.dll")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件: ")
endif()
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Private dependency sources")
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent Test)
qt_standard_project_setup(REQUIRES 6.8)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/jsoncpp" "${CMAKE_BINARY_DIR}/jsoncpp")
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS_DIR}/zlib" "${CMAKE_BINARY_DIR}/zlib")
add_subdirectory("${DEPS_DIR}/angelscript/angelscript/projects/cmake" "${CMAKE_BINARY_DIR}/angelscript")
set(AS_ADDON "${DEPS_DIR}/angelscript/add_on")
add_library(script_addons STATIC
  "${AS_ADDON}/scriptstdstring/scriptstdstring.cpp"
  "${AS_ADDON}/scriptstdstring/scriptstdstring_utils.cpp"
  "${AS_ADDON}/scriptarray/scriptarray.cpp"
  "${AS_ADDON}/scriptdictionary/scriptdictionary.cpp"
  "${AS_ADDON}/scriptany/scriptany.cpp"
  "${AS_ADDON}/datetime/datetime.cpp"
  "${AS_ADDON}/scriptmath/scriptmath.cpp")
target_include_directories(script_addons PUBLIC "${DEPS_DIR}/angelscript/angelscript/include" "${AS_ADDON}")
target_compile_definitions(script_addons PUBLIC AS_USE_STLNAMES=1)
target_link_libraries(script_addons PUBLIC angelscript)
add_library(player_core STATIC
  player/src/domain/Models.cpp
  player/src/network/HttpService.cpp
  player/src/media/live/FlvBuffer.cpp
  player/src/media/live/LiveTimeshift.cpp
  player/src/scripts/JsonBinding.cpp
  player/src/scripts/HostApi.cpp
  player/src/scripts/ScriptRuntime.cpp
  player/src/storage/Settings.cpp)
target_include_directories(player_core PUBLIC player/src)
target_link_libraries(player_core PUBLIC Qt6::Core Qt6::Network script_addons jsoncpp_static zlibstatic)
target_include_directories(player_core PRIVATE "${DEPS_DIR}/zlib" "${CMAKE_BINARY_DIR}/zlib")
target_compile_options(player_core PRIVATE /W4)
option(BUILD_PLAYER "Build native GUI" ON)
if(BUILD_PLAYER)
set(DS_BASE "${DEPS_DIR}/lav-source/LAVFilters-0.80/common/baseclasses")
add_library(dshow_base STATIC "${DS_BASE}/amfilter.cpp" "${DS_BASE}/combase.cpp" "${DS_BASE}/mtype.cpp" "${DS_BASE}/wxlist.cpp" "${DS_BASE}/wxutil.cpp" "${DS_BASE}/wxdebug.cpp" "${DS_BASE}/ctlutil.cpp")
target_include_directories(dshow_base PUBLIC "${DS_BASE}")
target_compile_options(dshow_base PRIVATE /permissive /Zc:strictStrings- /UNOMINMAX)
target_link_libraries(dshow_base PUBLIC strmiids ole32 oleaut32 winmm)
qt_add_executable(SimplePlayer
  player/resources/app.manifest
  player/src/app/main.cpp
  tests/FinalFeaturesProbe.cpp
  tests/PlaybackControlsProbe.cpp
  tests/SmokeProbe.cpp
  player/src/app/PlayerController.cpp
  player/src/media/directshow/GraphSession.cpp
  player/src/media/directshow/FilterLoader.cpp
  player/src/media/directshow/HlsCompatibility.cpp
  player/src/media/subtitles/SubtitleProvider.cpp
  player/src/media/subtitles/TextSubtitleSink.cpp
  player/src/accounts/webview2/BrowserSession.cpp)
target_include_directories(SimplePlayer PRIVATE tests "${DEPS_DIR}/webview2/build/native/include" "${DEPS_DIR}/interfaces")
target_link_libraries(SimplePlayer PRIVATE player_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 strmiids ole32 oleaut32 uuid shlwapi dxgi d3d11)
target_link_libraries(SimplePlayer PRIVATE dshow_base)
set_source_files_properties(player/src/media/subtitles/TextSubtitleSink.cpp PROPERTIES COMPILE_OPTIONS "/Zc:strictStrings-")
target_include_directories(SimplePlayer PRIVATE "${DEPS_DIR}/vcpkg-installed/x64-windows/include")
target_link_libraries(SimplePlayer PRIVATE "${DEPS_DIR}/vcpkg-installed/x64-windows/lib/ass.lib")
set_source_files_properties(player/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(SimplePlayer URI SimplePlayer VERSION 1.0 QML_FILES player/qml/Main.qml player/qml/Theme.qml player/qml/PlaybackBar.qml player/qml/PlaybackButton.qml player/qml/TitleStrip.qml player/qml/NativeTip.qml player/qml/StableMenu.qml player/qml/StableComboBox.qml player/qml/PathEntry.qml player/qml/SettingsWindow.qml player/qml/ScriptDebugWindow.qml player/qml/SubtitleMenu.qml player/qml/SubtitleTrackMenu.qml player/qml/WindowButton.qml)
set_target_properties(SimplePlayer PROPERTIES WIN32_EXECUTABLE TRUE)
qt_target_qml_sources(SimplePlayer QML_FILES player/qml/RepeatMenu.qml)
qt_target_qml_sources(SimplePlayer QML_FILES player/qml/TextEditMenu.qml)
qt_target_qml_sources(SimplePlayer QML_FILES player/qml/CheckMark.qml player/qml/PlayerMenuItem.qml player/qml/PlayerCheckBox.qml)
set_source_files_properties(player/qml/MetadataFormat.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_target_qml_sources(SimplePlayer QML_FILES player/qml/MetadataFormat.qml player/qml/MediaInfoWindow.qml)
qt_target_qml_sources(SimplePlayer QML_FILES player/qml/DiagnosticWindow.qml)
target_sources(SimplePlayer PRIVATE tests/DiagnosticProbe.cpp)
endif()
enable_testing()
qt_add_executable(player_tests tests/PlayerTests.cpp)
target_link_libraries(player_tests PRIVATE player_core Qt6::Test Qt6::Concurrent)
add_test(NAME contracts COMMAND player_tests -o "${CMAKE_BINARY_DIR}/contracts-results.txt,txt")
qt_add_executable(live_tests tests/LiveBufferTests.cpp)
target_link_libraries(live_tests PRIVATE player_core Qt6::Test)
add_test(NAME live_buffer_contracts COMMAND live_tests -o "${CMAKE_BINARY_DIR}/live-results.txt,txt")
qt_add_executable(script_probe tests/ScriptProbe.cpp player/src/media/directshow/FilterLoader.cpp player/src/media/directshow/HlsCompatibility.cpp)
target_link_libraries(script_probe PRIVATE player_core strmiids ole32)
if(BUILD_PLAYER)
qt_add_executable(subtitle_tests tests/SubtitleTests.cpp player/src/media/subtitles/SubtitleProvider.cpp player/src/media/directshow/FilterLoader.cpp player/src/media/directshow/HlsCompatibility.cpp)
target_include_directories(subtitle_tests PRIVATE player/src "${DEPS_DIR}/interfaces" "${DEPS_DIR}/vcpkg-installed/x64-windows/include")
target_link_libraries(subtitle_tests PRIVATE player_core Qt6::Test Qt6::Gui strmiids ole32 "${DEPS_DIR}/vcpkg-installed/x64-windows/lib/ass.lib")
add_test(NAME subtitle_contracts COMMAND subtitle_tests -o "${CMAKE_BINARY_DIR}/subtitle-results.txt,txt")
install(TARGETS SimplePlayer RUNTIME DESTINATION .)
install(FILES "${DEPS_DIR}/webview2/build/native/x64/WebView2Loader.dll" DESTINATION .)
install(FILES LICENSE README.md packaging/source-manifest.json packaging/validated-filter-baseline.json packaging/THIRD_PARTY_NOTICES.md DESTINATION .)
if(EXISTS "${CMAKE_SOURCE_DIR}/docs")
  install(DIRECTORY docs/ DESTINATION docs FILES_MATCHING PATTERN "*.md" PATTERN "*.json")
endif()
if(EXISTS "${DEPS_DIR}/live-cache/ffmpeg.exe")
  install(FILES "${DEPS_DIR}/live-cache/ffmpeg.exe" "${DEPS_DIR}/live-cache/FFMPEG-LICENSE.txt" DESTINATION helpers)
  if(EXISTS "${DEPS_DIR}/live-cache/manifest.json")
    install(FILES "${DEPS_DIR}/live-cache/manifest.json" DESTINATION helpers)
  endif()
endif()
install(DIRECTORY player/resources/themes DESTINATION resources)
if(EXISTS "${DEPS_DIR}/mpcaudio/standalone_filters-mpc-be.1.9.1.x64/MpcAudioRenderer.ax")
install(FILES "${DEPS_DIR}/mpcaudio/standalone_filters-mpc-be.1.9.1.x64/MpcAudioRenderer.ax" DESTINATION filters)
install(FILES "${DEPS_DIR}/mpcaudio/standalone_filters-mpc-be.1.9.1.x64/LICENSE.txt" "${DEPS_DIR}/mpcaudio/standalone_filters-mpc-be.1.9.1.x64/Authors.txt" DESTINATION licenses/mpcaudio)
endif()
install(DIRECTORY "${DEPS_DIR}/vcpkg-installed/x64-windows/bin/" DESTINATION . FILES_MATCHING PATTERN "*.dll")
install(DIRECTORY "${DEPS_DIR}/vcpkg-installed/x64-windows/share/" DESTINATION licenses/libass-dependencies FILES_MATCHING PATTERN "copyright")
endif()
