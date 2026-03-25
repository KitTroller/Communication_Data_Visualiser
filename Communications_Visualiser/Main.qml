import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Communications_Visualiser // This exposes your C++ SerialHandler to QML

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")
}
