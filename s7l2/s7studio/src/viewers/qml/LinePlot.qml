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
