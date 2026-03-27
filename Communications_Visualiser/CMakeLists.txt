cmake_minimum_required(VERSION 3.16)

project(Communications_Visualiser VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick SerialPort Charts Widgets)

find_library(LIQUID_DSP_LIB liquid)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appCommunications_Visualiser
    main.cpp
)

qt_add_qml_module(appCommunications_Visualiser
    URI Communications_Visualiser
    QML_FILES
        Main.qml
        DashboardPage.qml
        ConstellationPage.qml
        SOURCES serialhandler.h serialhandler.cpp
    RESOURCES
        assets/background.png
        QML_FILES ConstellationPage.qml
        QML_FILES DashboardPage.qml
        SOURCES dspengine.h
        SOURCES dspengine.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appCommunications_Visualiser PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appCommunications_Visualiser
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appCommunications_Visualiser
    PRIVATE
    Qt6::Quick
    Qt6::SerialPort
    Qt6::Charts
    Qt6::Widgets
    ${LIQUID_DSP_LIB}
)

install(TARGETS appCommunications_Visualiser
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
