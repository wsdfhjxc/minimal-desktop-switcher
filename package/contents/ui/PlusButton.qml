import QtQuick 2.1
import QtQuick.Controls 1.4

Item {
    implicitWidth: label.width
    height: parent.implicitHeight
    visible: plasmoid.configuration.showPlusButton

    Label {
        id: label
        text: "➕"
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: plasmoid.configuration.labelSize || theme.defaultFont.pixelSize
        color: plasmoid.configuration.labelColor || theme.textColor
        font.family: plasmoid.configuration.labelFont || theme.defaultFont.family
    }

    MouseArea {
        id: mouseArea
        hoverEnabled: true
        anchors.fill: parent

        onClicked: {
            virtualDesktopBar.addNewDesktop();
        }
    }

    state: {
        return mouseArea.containsMouse ? "hovered" : "default"
    }

    states: [
        State {
            name: "default"
            PropertyChanges {
                target: label
                opacity: plasmoid.configuration.dimLabelForIdle ? 0.7 : 0.9
            }
        },

        State {
            name: "hovered"
            PropertyChanges {
                target: label
                opacity: plasmoid.configuration.dimLabelForIdle ? 0.8 : 0.9
            }
        }
    ]

    transitions: [
        Transition {
            enabled: plasmoid.configuration.enableAnimations
            to: "hovered"
            ParallelAnimation {
                NumberAnimation {
                    target: label
                    property: "opacity"
                    duration: 150
                }
            }
        },

        Transition {
            enabled: plasmoid.configuration.enableAnimations
            to: "default"
            ParallelAnimation {
                NumberAnimation {
                    target: label
                    property: "opacity"
                    duration: 300
                }
            }
        }
    ]
}
