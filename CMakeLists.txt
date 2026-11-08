cmake_minimum_required(VERSION 3.21)

project(MarkdownReader VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(markdown_reader_core STATIC
    src/AnchorResolver.cpp src/AnchorResolver.h
    src/DocumentManager.cpp src/DocumentManager.h
    src/DocumentOutline.cpp src/DocumentOutline.h
    src/DocumentTab.cpp src/DocumentTab.h
    src/MainWindow.cpp src/MainWindow.h
    src/MarkdownRenderer.cpp src/MarkdownRenderer.h
)
target_include_directories(markdown_reader_core PUBLIC src)
target_link_libraries(markdown_reader_core PUBLIC Qt6::Widgets)

qt_add_executable(MarkdownReader WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(MarkdownReader PRIVATE markdown_reader_core)
target_compile_definitions(MarkdownReader PRIVATE MARKDOWN_READER_VERSION="${PROJECT_VERSION}")
set_target_properties(MarkdownReader PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Markdown Reader"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.markdownreader.app
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(markdown_reader_tests tests/ReaderTest.cpp)
    set_target_properties(markdown_reader_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(markdown_reader_tests PRIVATE markdown_reader_core Qt6::Test)
    add_test(NAME markdown_reader_tests
        COMMAND markdown_reader_tests -o -,txt -o qt-test.txt,txt
    )
    set_tests_properties(markdown_reader_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
