// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// 2D line plot for Studio observation viewers. All view state (axis ranges,
// titles, legend, series) belongs to the C++ LinePlotBridge passed in as
// `plot`; gestures are forwarded to it rather than changing axes here.
import QtQuick
import QtGraphs

Item {
    id: root
    objectName: "viewer.linePlot"
    required property QtObject plot

    // Axis titles sit outside the GraphsView so they never cover tick labels.
    Text {
        id: yTitle
        visible: text.length > 0
        text: plot.yTitle
        rotation: -90
        x: -width / 2 + height / 2 + 2
        y: view.y + view.plotArea.y + view.plotArea.height / 2 - height / 2
        font.pixelSize: 12
        color: "#303030"
    }
    Text {
        id: xTitle
        visible: text.length > 0
        text: plot.xTitle
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        x: view.x + view.plotArea.x + view.plotArea.width / 2 - width / 2
        font.pixelSize: 12
        color: "#303030"
    }

    GraphsView {
        id: view
        objectName: "viewer.graphs"
        anchors.fill: parent
        anchors.leftMargin: yTitle.visible ? yTitle.height + 4 : 0
        anchors.bottomMargin: xTitle.visible ? xTitle.height + 4 : 0
        marginLeft: 8
        marginRight: 16
        marginTop: 12
        marginBottom: 8

        theme: GraphsTheme {
            colorScheme: GraphsTheme.ColorScheme.Light
            backgroundColor: "white"
            plotAreaBackgroundColor: "white"
            grid.mainColor: "#d8d8d8"
            grid.subColor: "#efefef"
            axisX.mainColor: "#606060"
            axisY.mainColor: "#606060"
            labelTextColor: "#303030"
        }

        axisX: ValueAxis {
            min: plot.xMin
            max: plot.xMax
            titleVisible: false
            labelFormat: "%.6g"
            subTickCount: 1
        }
        axisY: ValueAxis {
            min: plot.yMin
            max: plot.yMax
            titleVisible: false
            labelFormat: "%.6g"
            subTickCount: 1
        }

        Component.onCompleted: {
            for (const series of plot.seriesObjects())
                view.addSeries(series)
        }

        Connections {
            target: plot
            function onSeriesAdded(series) { view.addSeries(series) }
            function onSeriesRemoved(series) { view.removeSeries(series) }
        }
    }

    Item {
        x: view.x + view.plotArea.x
        y: view.y + view.plotArea.y
        width: view.plotArea.width
        height: view.plotArea.height
        clip: true
        visible: plot.densityVisible
        Image {
            objectName: "viewer.density"
            source: plot.densitySource
            cache: false
            asynchronous: false
            smooth: false
            x: (plot.densityBounds.x - plot.xMin) / (plot.xMax - plot.xMin) * parent.width
            y: (plot.yMax - plot.densityBounds.y - plot.densityBounds.height) / (plot.yMax - plot.yMin) * parent.height
            width: plot.densityBounds.width / (plot.xMax - plot.xMin) * parent.width
            height: plot.densityBounds.height / (plot.yMax - plot.yMin) * parent.height
        }
    }

    Item {
        x: view.x + view.plotArea.x
        y: view.y + view.plotArea.y
        width: view.plotArea.width
        height: view.plotArea.height
        clip: true
        Repeater {
            model: plot.selectionMarkers
            delegate: Item {
                required property var modelData
                objectName: "viewer.cursor." + modelData.label
                width: parent.width
                height: parent.height
                property real px: Math.min(Math.max(0, (modelData.x - plot.xMin) / (plot.xMax - plot.xMin) * width), Math.max(0, width - 2))
                property real py: (plot.yMax - modelData.y) / (plot.yMax - plot.yMin) * height
                property color markerColor: modelData.label === "A" ? "#a02b8a" : "#007b70"
                visible: modelData.x >= plot.xMin && modelData.x <= plot.xMax
                         && (modelData.vertical || (modelData.y >= plot.yMin && modelData.y <= plot.yMax))
                Rectangle {
                    x: parent.px - (modelData.vertical ? 0 : 6)
                    y: modelData.vertical ? 0 : parent.py - 6
                    width: modelData.vertical ? 2 : 12
                    height: modelData.vertical ? parent.height : 12
                    color: modelData.vertical ? parent.markerColor : "transparent"
                    border.width: modelData.vertical ? 0 : 2
                    border.color: parent.markerColor
                }
                Text {
                    text: modelData.label
                    x: Math.min(parent.px + 5, parent.width - width - 2)
                    y: modelData.vertical ? (modelData.label === "A" ? 0 : 16) : parent.py - 18
                    color: parent.markerColor
                    font.bold: true
                }
            }
        }
    }
    Item {
        visible: plot.triggerVisible && plot.xMin <= 0 && plot.xMax >= 0
        x: Math.min(view.x + view.plotArea.x - plot.xMin / (plot.xMax - plot.xMin) * view.plotArea.width,
                    view.x + view.plotArea.x + view.plotArea.width - 2)
        y: view.y + view.plotArea.y
        height: view.plotArea.height
        Rectangle { width: 1; height: parent.height; color: "#ed6c02" }
        Text { text: "T"; x: -10; color: "#b34a00"; font.bold: true }
    }
    TapHandler {
        enabled: plot.inspectionEnabled
        acceptedButtons: Qt.LeftButton
        onTapped: (eventPoint, button) => {
            const area = view.plotArea
            if (area.width > 0 && area.height > 0)
                plot.pick((eventPoint.position.x - view.x - area.x) / area.width,
                          1 - (eventPoint.position.y - view.y - area.y) / area.height)
        }
    }
    HoverHandler {
        id: inspectionHover
        enabled: plot.inspectionEnabled
        function updateReadout() {
            const area = view.plotArea
            const x = point.position.x - view.x - area.x
            const y = point.position.y - view.y - area.y
            plot.hover(area.width > 0 ? x / area.width : 0,
                       hovered && x >= 0 && x <= area.width && y >= 0 && y <= area.height)
            plot.hoverPoint(area.width > 0 ? x / area.width : 0, area.height > 0 ? 1 - y / area.height : 0,
                       hovered && x >= 0 && x <= area.width && y >= 0 && y <= area.height)
        }
        onPointChanged: updateReadout()
        onHoveredChanged: updateReadout()
    }
    Rectangle {
        objectName: "viewer.sampleCursor"
        visible: plot.inspectionEnabled && plot.cursorVisible
                 && plot.cursorX >= plot.xMin && plot.cursorX <= plot.xMax
        x: view.x + view.plotArea.x + (plot.cursorX - plot.xMin) / (plot.xMax - plot.xMin) * view.plotArea.width
        y: view.y + view.plotArea.y
        width: 1
        height: view.plotArea.height
        color: "#707070"
    }

    // Wheel: zoom both axes about the cursor. Drag: pan.
    WheelHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const area = view.plotArea
            if (area.width <= 0 || area.height <= 0)
                return
            const fx = (event.x - view.x - area.x) / area.width
            const fy = 1 - (event.y - view.y - area.y) / area.height
            const steps = event.angleDelta.y / 120
            if (steps !== 0)
                plot.zoom(fx, fy, Math.pow(0.85, steps))
        }
    }

    DragHandler {
        id: drag
        target: null
        property point last: Qt.point(0, 0)
        onActiveChanged: last = Qt.point(0, 0)
        onTranslationChanged: {
            const area = view.plotArea
            if (!active || area.width <= 0 || area.height <= 0)
                return
            const dx = translation.x - last.x
            const dy = translation.y - last.y
            last = Qt.point(translation.x, translation.y)
            plot.pan(-dx / area.width, dy / area.height)
        }
    }

    Rectangle {
        id: legend
        objectName: "viewer.legend"
        visible: plot.legendVisible && plot.legend.length > 0
        x: view.x + view.plotArea.x + view.plotArea.width - width - 8
        y: view.y + view.plotArea.y + 8
        width: legendColumn.implicitWidth + 16
        height: legendColumn.implicitHeight + 10
        color: "#f2ffffff"
        border.color: "#a0a0a0"
        radius: 2

        Column {
            id: legendColumn
            x: 8
            y: 5
            spacing: 2
            Repeater {
                model: plot.legend
                Row {
                    id: entry
                    required property var modelData
                    spacing: 6
                    Item {
                        width: 26
                        height: entryText.height
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            // Overlays (saved runs) show a dashed swatch.
                            Repeater {
                                model: entry.modelData.overlay ? 4 : 1
                                Rectangle {
                                    width: entry.modelData.overlay ? 4 : 26
                                    height: 2
                                    color: entry.modelData.color
                                    opacity: entry.modelData.overlay ? 0.7 : 1
                                }
                            }
                        }
                    }
                    Text {
                        id: entryText
                        text: entry.modelData.name
                        font.pixelSize: 12
                        color: "#202020"
                    }
                }
            }
        }
    }
}
