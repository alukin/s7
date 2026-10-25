// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// 2D line plot for Studio observation viewers. All view state (axis ranges,
// titles, legend, series) belongs to the C++ LinePlotBridge passed in as
// `plot`; gestures are forwarded to it rather than changing axes here.
import QtQuick
import QtGraphs

Rectangle {
    id: root
    color: plot.instrumentTheme ? "#141a21" : "white"
    objectName: "viewer.linePlot"
    required property QtObject plot

    // Small dock previews need fewer labels than the full viewer.
    function compactTickStep(span, divisions) {
        const raw = span / Math.max(2, divisions)
        if (!(raw > 0) || !isFinite(raw)) return 0
        const decade = Math.pow(10, Math.floor(Math.log10(raw)))
        const fraction = raw / decade
        const step = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 2.5 ? 2.5 : fraction <= 5 ? 5 : 10
        return step * decade
    }

    function verticalTickFormat(span, divisions) {
        const step = Math.abs(span) / divisions
        if (!(step > 0) || step < 1e-6 || step >= 1e6) return "%.4e"
        // Fixed precision near ordinary scales keeps rounding residue around
        // zero from producing a long scientific label across the axis title.
        return "%." + Math.max(0, 2 - Math.floor(Math.log10(step))) + "f"
    }

    // Axis titles sit outside the GraphsView so they never cover tick labels.
    Text {
        id: yTitle
        visible: text.length > 0
        text: plot.yTitle
        rotation: -90
        x: -width / 2 + height / 2 + 2
        y: view.y + view.plotArea.y + view.plotArea.height / 2 - height / 2
        font.pixelSize: 12
        color: plot.instrumentTheme ? "#a8b7c8" : "#303030"
    }
    Text {
        id: xTitle
        visible: text.length > 0
        text: plot.xTitle
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        x: view.x + view.plotArea.x + view.plotArea.width / 2 - width / 2
        font.pixelSize: 12
        color: plot.instrumentTheme ? "#a8b7c8" : "#303030"
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
            theme: GraphsTheme.Theme.UserDefined
            colorScheme: plot.instrumentTheme ? GraphsTheme.ColorScheme.Dark : GraphsTheme.ColorScheme.Light
            backgroundColor: plot.instrumentTheme ? "#141a21" : "white"
            plotAreaBackgroundColor: plot.instrumentTheme ? "#141a21" : "white"
            grid.mainColor: plot.instrumentTheme ? "#35404d" : "#d8d8d8"
            grid.mainWidth: 1
            grid.subWidth: 0.5
            grid.subColor: plot.instrumentTheme ? "#222c37" : "#efefef"
            axisX.mainColor: plot.instrumentTheme ? "#758698" : "#606060"
            axisY.mainColor: plot.instrumentTheme ? "#758698" : "#606060"
            labelTextColor: plot.instrumentTheme ? "#a8b7c8" : "#303030"
        }

        axisX: ValueAxis {
            min: plot.xMin
            max: plot.xMax
            titleVisible: false
            labelFormat: plot.compactAxes ? "%.5g" : "%.6g"
            tickInterval: plot.timeDivisions ? (max-min)/10 : plot.compactAxes ? root.compactTickStep(max-min, Math.floor((view.width-90)/70)) : 0
            tickAnchor: plot.timeDivisions ? min : 0
            subTickCount: 1
        }
        axisY: ValueAxis {
            min: plot.yMin
            max: plot.yMax
            titleVisible: false
            labelFormat: plot.verticalDivisions > 0 ? root.verticalTickFormat(max-min, plot.verticalDivisions) : plot.compactAxes ? "%.5g" : "%.6g"
            tickInterval: plot.verticalDivisions > 0 ? (max-min)/plot.verticalDivisions : plot.compactAxes ? root.compactTickStep(max-min, Math.floor((view.height-60)/40)) : 0
            tickAnchor: 0
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

    Binding {
        target: plot
        property: "plotAreaSize"
        value: Qt.size(view.plotArea.width, view.plotArea.height)
    }

    Item {
        x: view.x + view.plotArea.x
        y: view.y + view.plotArea.y
        width: view.plotArea.width
        height: view.plotArea.height
        clip: true
        // Rings on the samples the pointer tooltip describes.
        Repeater {
            model: plot.hoverMarkers
            delegate: Rectangle {
                required property var modelData
                objectName: "viewer.hoverMarker"
                x: (modelData.x - plot.xMin) / (plot.xMax - plot.xMin) * parent.width - width / 2
                y: (plot.yMax - modelData.y) / (plot.yMax - plot.yMin) * parent.height - height / 2
                width: 9
                height: 9
                radius: 4.5
                color: "#ccffffff"
                border.width: 2
                border.color: modelData.color
            }
        }
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
    // A wheel zoom moves the data under a still pointer; refresh its readout.
    Connections {
        target: plot
        enabled: inspectionHover.hovered
        property bool busy: false
        function refresh() { if (!busy) { busy = true; inspectionHover.updateReadout(); busy = false } }
        function onXRangeChanged() { refresh() }
        function onYRangeChanged() { refresh() }
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

    // Owners choose horizontal-only or two-axis navigation in the bridge.
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
        width: plot.selectedLegend >= 0 ? Math.min(260, Math.max(100, view.plotArea.width * 0.55)) : legendColumn.implicitWidth + 16
        height: legendColumn.implicitHeight + 10
        color: plot.instrumentTheme ? "#ee202a35" : "#f2ffffff"
        border.color: plot.instrumentTheme ? "#455366" : "#a0a0a0"
        radius: 2

        Column {
            id: legendColumn
            x: 8
            y: 5
            width: plot.selectedLegend >= 0 ? legend.width - 16 : implicitWidth
            spacing: 2
            Repeater {
                model: plot.legend
                Row {
                    id: entry
                    required property var modelData
                    spacing: 6
                    width: plot.selectedLegend >= 0 ? legendColumn.width : implicitWidth
                    objectName: "viewer.legend.channel." + modelData.index
                    activeFocusOnTab: plot.checkableLegend
                    Accessible.role: plot.checkableLegend ? Accessible.CheckBox : Accessible.StaticText
                    Accessible.name: modelData.name + (plot.selectedLegend === modelData.index ? " (selected channel)" : "")
                    Accessible.checkable: plot.checkableLegend
                    Accessible.checked: modelData.checked
                    Accessible.onPressAction: if (plot.checkableLegend) plot.toggleLegend(modelData.index, !modelData.checked)
                    Keys.onSpacePressed: if (plot.checkableLegend) plot.toggleLegend(modelData.index, !modelData.checked)
                    Keys.onReturnPressed: if (plot.selectedLegend >= 0) plot.selectLegend(modelData.index)
                    Keys.onEnterPressed: if (plot.selectedLegend >= 0) plot.selectLegend(modelData.index)
                    Item {
                        width: plot.checkableLegend ? 18 : 26
                        height: entryText.height
                        MouseArea {
                            anchors.fill: parent
                            enabled: plot.checkableLegend
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { entry.forceActiveFocus(); plot.toggleLegend(entry.modelData.index, !entry.modelData.checked) }
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            visible: plot.checkableLegend
                            width: 14; height: 14; radius: 2
                            border.width: 2; border.color: entry.modelData.color
                            color: entry.modelData.checked ? entry.modelData.color : "transparent"
                            Text { anchors.centerIn: parent; text: "✓"; color: "white"; font.pixelSize: 12; visible: entry.modelData.checked }
                        }
                        Row {
                            visible: !plot.checkableLegend
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
                        objectName: "viewer.legend.name." + entry.modelData.index
                        text: entry.modelData.name
                        width: plot.selectedLegend >= 0 ? Math.max(0, entry.width - (plot.checkableLegend ? 18 : 26) - entry.spacing) : implicitWidth
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.bold: plot.selectedLegend === entry.modelData.index
                        color: plot.selectedLegend === entry.modelData.index ? entry.modelData.color : plot.instrumentTheme ? "#dde4ed" : "#202020"
                        MouseArea {
                            anchors.fill: parent
                            enabled: plot.selectedLegend >= 0
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { entry.forceActiveFocus(); plot.selectLegend(entry.modelData.index) }
                        }
                    }
                }
            }
        }
    }

    // Translucent value box beside the pointer; it flips sides near the edges.
    Rectangle {
        id: hoverTip
        objectName: "viewer.hoverTip"
        readonly property point pointer: inspectionHover.point.position
        visible: plot.inspectionEnabled && inspectionHover.hovered && plot.hoverTip.length > 0
        width: tipColumn.implicitWidth + 14
        height: tipColumn.implicitHeight + 10
        x: pointer.x + 18 + width <= root.width ? pointer.x + 18 : Math.max(0, pointer.x - 18 - width)
        y: Math.max(0, Math.min(pointer.y + 18 + height <= root.height ? pointer.y + 18 : pointer.y - 18 - height,
                                root.height - height))
        color: "#d9ffffff"
        border.color: "#99707070"
        radius: 4

        Column {
            id: tipColumn
            x: 7
            y: 5
            spacing: 2
            Repeater {
                model: plot.hoverTip
                Row {
                    id: tipRow
                    required property var modelData
                    spacing: 6
                    Rectangle {
                        visible: tipRow.modelData.color !== undefined
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 3
                        color: tipRow.modelData.color !== undefined ? tipRow.modelData.color : "transparent"
                    }
                    Text {
                        text: tipRow.modelData.text
                        font.pixelSize: 12
                        color: tipRow.modelData.color !== undefined ? "#202020" : "#505050"
                    }
                }
            }
        }
    }
}
