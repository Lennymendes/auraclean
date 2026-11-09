# ==============================================================================
#  AuraClean — NextDSP / AuraSuite
#  Gestionnaire & désinstalleur de plugins audio (VST3 / AU / AAX) — macOS only
#  Application GUI Standalone (JUCE 8, C++20, Objective-C++)
# ==============================================================================
cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Version minimale de macOS")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build Universal (Apple Silicon + Intel)")

project(AuraClean VERSION 1.0.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "AuraClean est une application macOS uniquement.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
#  JUCE : copie partagée de la AuraSuite si fournie, sinon FetchContent
#    cmake -B build -G Xcode -DAURASUITE_JUCE_DIR=/chemin/vers/JUCE
# ------------------------------------------------------------------------------
set(AURASUITE_JUCE_DIR "" CACHE PATH "Chemin vers JUCE 8 (vide = téléchargement automatique)")

if(AURASUITE_JUCE_DIR AND EXISTS "${AURASUITE_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${AURASUITE_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
#  Application
# ------------------------------------------------------------------------------
juce_add_gui_app(AuraClean
    PRODUCT_NAME                 "AuraClean"
    COMPANY_NAME                 "NextDSP"
    COMPANY_WEBSITE              "https://nextdsp.com"
    BUNDLE_ID                    "com.nextdsp.auraclean"
    VERSION                      ${PROJECT_VERSION}
    HARDENED_RUNTIME_ENABLED     TRUE
    HARDENED_RUNTIME_OPTIONS     "com.apple.security.automation.apple-events"
    PLIST_TO_MERGE               "<plist><dict>
        <key>NSAppleEventsUsageDescription</key>
        <string>AuraClean needs administrator authorisation to move system-wide plug-ins.</string>
        <key>LSApplicationCategoryType</key>
        <string>public.app-category.music</string>
        <key>NSRequiresAquaSystemAppearance</key>
        <false/>
    </dict></plist>")

target_sources(AuraClean PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/AuraLookAndFeel.h
    Source/PluginScanner.cpp
    Source/PluginScanner.h
    Source/MachOInspector.h
    Source/UninstallerMac.h
    Source/UninstallerMac.mm)

# Seul fichier ObjC++ de l'app : ARC activé (les modules JUCE restent en MRC)
set_source_files_properties(Source/UninstallerMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

target_compile_definitions(AuraClean PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:AuraClean,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:AuraClean,JUCE_VERSION>")

target_link_libraries(AuraClean
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_gui_basics
        "-framework CoreFoundation"
        "-framework Foundation"
        "-framework AppKit"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
