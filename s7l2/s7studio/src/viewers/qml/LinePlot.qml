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
    property bool markerDragging: false
    onMarkerDraggingChanged: if (markerDragging) { plot.hover(0, false); plot.hoverPoint(0, 0, false) }
    readonly property bool triggerHandles: !!plot.instrumentTrigger.enabled
    readonly property real areaLeft: view.x + view.plotArea.x
    readonly property real areaTop: view.y + view.plotArea.y
    function bounded(value, low, high) { return Math.max(low, Math.min(high, value)) }
    function markerY(value) { return bounded((plot.yMax-value)/(plot.yMax-plot.yMin)*view.plotArea.height, 0, view.plotArea.height) }
    readonly property int instrumentTickStride: {
        if (!plot.channelZeroMarkers.length) return 1
        const divisionPixels = Math.max(1, view.width-view.marginLeft-view.marginRight-30)/10
        for (const stride of [1, 2, 5]) if (divisionPixels*stride >= 88) return stride
        return 10
    }
    // Fan coincident badges in the margin. Leaders and arrow tips still point
    // to the true zero, so no display offset is invented to separate handles.
    function zeroBadgeCenter(channel) {
        const entries = []
        const height = view.plotArea.height
        for (let i = 0; i < plot.channelZeroMarkers.length; ++i)
            if (plot.channelZeroMarkers[i].visible)
                entries.push({channel: i, y: bounded(markerY(plot.channelZeroMarkers[i].value), 11, Math.max(11, height-11))})
        entries.sort((a, b) => a.y-b.y || a.channel-b.channel)
        const gap = Math.min(24, Math.max(1, (height-22)/Math.max(1, entries.length-1)))
        for (let i = 1; i < entries.length; ++i) entries[i].y = Math.max(entries[i].y, entries[i-1].y+gap)
        if (entries.length && entries[entries.length-1].y > height-11) {
            entries[entries.length-1].y = height-11
            for (let i = entries.length-2; i >= 0; --i) entries[i].y = Math.min(entries[i].y, entries[i+1].y-gap)
        }
        for (const entry of entries) if (entry.channel === channel) return entry.y
        return 11
    }

    component InstrumentHandle: Rectangle {
        id: handle
        property real value: 0
        property real minimum: 0
        property real maximum: 1
        property real keyboardStep: (maximum-minimum)/100
        property bool horizontal: false
        property bool resettable: false
        property bool highlighted: false
        property string label: ""
        property string accessibleLabel: ""
        property string help: accessibleLabel
        property color accent: "#ffaf58"
        property real candidate: value
        property bool externalPressed: false
        readonly property bool dragging: (markerMouse.pressed || externalPressed) && markerMouse.moved
        function beginExternal(point) { externalPressed = true; markerMouse.begin(point) }
        function moveExternal(point) { if (externalPressed && !markerMouse.reset) markerMouse.updatePoint(point) }
        function finishExternal() { markerMouse.finish(); externalPressed = false }
        function cancelExternal() { markerMouse.cancel(); externalPressed = false }
        function resetPosition() { markerMouse.resetEdit() }
        readonly property real displayedValue: dragging ? candidate : value
        signal edited(real value)
        signal previewed(real value)
        signal selected()
        width: 26; height: 22; radius: 3
        color: markerMouse.containsMouse || activeFocus || markerMouse.pressed || externalPressed ? "#34475a" : "#202a35"
        border.color: accent
        border.width: activeFocus || highlighted ? 2 : 1
        activeFocusOnTab: visible
        onActiveFocusChanged: plot.markerFocus = activeFocus
        onVisibleChanged: if (!visible && activeFocus) root.forceActiveFocus()
        Accessible.role: Accessible.Slider
        Accessible.name: accessibleLabel
        Accessible.description: "Drag or use arrow keys. Shift uses smaller vertical steps." + (resettable ? " Home or double-click resets position to zero." : "")
        Accessible.focusable: true
        Accessible.onIncreaseAction: edited(root.bounded(value+keyboardStep, minimum, maximum))
        Accessible.onDecreaseAction: edited(root.bounded(value-keyboardStep, minimum, maximum))
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape && (markerMouse.pressed || externalPressed)) {
                markerMouse.reset = true; markerMouse.moved = false; root.markerDragging = false
                event.accepted = true; return
            }
            let direction = 0
            if (horizontal && event.key === Qt.Key_Right || !horizontal && event.key === Qt.Key_Up) direction = 1
            if (horizontal && event.key === Qt.Key_Left || !horizontal && event.key === Qt.Key_Down) direction = -1
            if (direction) {
                selected()
                edited(root.bounded(value+direction*keyboardStep*(!horizontal && event.modifiers & Qt.ShiftModifier ? 0.1 : 1), minimum, maximum))
                event.accepted = true
            } else if (event.key === Qt.Key_Home && (resettable || horizontal)) {
                selected(); edited(resettable ? 0 : minimum); event.accepted = true
            } else if (event.key === Qt.Key_End && horizontal) {
                edited(maximum); event.accepted = true
            }
        }
        Text { anchors.centerIn: parent; text: handle.label; color: handle.accent; font.pixelSize: 11; font.bold: true }
        Rectangle {
            visible: markerMouse.containsMouse || handle.dragging
            z: 20
            width: Math.min(240, Math.max(140, root.width-12)); height: helpText.height+12
            x: handle.mapToItem(root, handle.width+6, 0).x+width > root.width ? -width-6 : handle.width+6
            y: Math.max(-handle.mapToItem(root, 0, 0).y, Math.min(0, root.height-handle.mapToItem(root, 0, 0).y-height))
            color: "#f0202a35"; border.color: handle.accent; radius: 3
            Text { id: helpText; x: 6; y: 6; width: parent.width-12; text: handle.help; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: "#e8eef7"; font.pixelSize: 12 }
        }
        MouseArea {
            id: markerMouse
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: handle.horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
            property point start: Qt.point(0, 0)
            property real startValue: 0
            property real span: 0
            property real pixels: 1
            property bool moved: false
            property bool reset: false
            property string revision: ""
            function updatePoint(point) {
                if (revision !== String(plot.instrumentTrigger.revision)) {
                    reset = true; moved = false; root.markerDragging = false; return
                }
                const delta = handle.horizontal ? point.x-start.x : start.y-point.y
                if (!moved && Math.abs(delta) < 2) return
                moved = true
                handle.candidate = root.bounded(startValue+delta/pixels*span, handle.minimum, handle.maximum)
                handle.previewed(handle.candidate)
            }
            function begin(point) {
                root.markerDragging = true; handle.forceActiveFocus(Qt.MouseFocusReason); handle.selected()
                revision = String(plot.instrumentTrigger.revision)
                start = point
                startValue = root.bounded(handle.value, handle.minimum, handle.maximum)
                span = handle.maximum-handle.minimum
                pixels = Math.max(1, handle.horizontal ? view.plotArea.width : view.plotArea.height)
                handle.candidate = handle.value; moved = false; reset = false
            }
            function finish() {
                if (!reset && moved && revision === String(plot.instrumentTrigger.revision)) handle.edited(handle.candidate)
                root.markerDragging = false
            }
            function cancel() { root.markerDragging = false; moved = false }
            function resetEdit() { if (handle.resettable) { reset = true; handle.selected(); handle.edited(0) } }
            onPressed: mouse => begin(mapToItem(root, mouse.x, mouse.y))
            onPositionChanged: mouse => { if (pressed && !reset) updatePoint(mapToItem(root, mouse.x, mouse.y)) }
            onReleased: finish()
            onCanceled: cancel()
            onDoubleClicked: resetEdit()
            onWheel: wheel => { wheel.accepted = true }
        }
    }

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

    function channelTickText(value, division) {
        if (!isFinite(value)) return "—"
        return Math.abs(value) < Math.abs(division) * 1e-8 ? "0" : value.toPrecision(5)
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
        color: plot.channelAxis ? plot.channelAxisColor : plot.instrumentTheme ? "#a8b7c8" : "#303030"
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
        marginLeft: (plot.channelAxis ? 50 : 8) + (plot.channelZeroMarkers.length ? 38 : 0)
        marginRight: root.triggerHandles ? 40 : 16
        marginTop: root.triggerHandles ? 28 : 12
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
            tickInterval: plot.timeDivisions ? (max-min)/10*root.instrumentTickStride : plot.compactAxes ? root.compactTickStep(max-min, Math.min(6, Math.floor((view.width-90)/70))) : 0
            tickAnchor: plot.timeDivisions ? min : 0
            subTickCount: plot.timeDivisions && root.instrumentTickStride > 1 ? root.instrumentTickStride-1 : 1
        }
        axisY: ValueAxis {
            min: plot.yMin
            max: plot.yMax
            titleVisible: false
            labelsVisible: !plot.channelAxis
            labelFormat: plot.verticalDivisions > 0 ? root.verticalTickFormat(max-min, plot.verticalDivisions) : plot.compactAxes ? "%.5g" : "%.6g"
            tickInterval: plot.verticalDivisions > 0 ? (max-min)/plot.verticalDivisions : plot.compactAxes ? root.compactTickStep(max-min, Math.min(6, Math.floor((view.height-60)/40))) : 0
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
        id: channelScale
        visible: plot.channelAxis
        x: view.x + 6
        y: view.y + view.plotArea.y
        width: Math.max(0, view.plotArea.x - 20 - (plot.channelZeroMarkers.length ? 38 : 0))
        height: view.plotArea.height
        readonly property real division: (plot.yMax-plot.yMin)/Math.max(1, plot.verticalDivisions)
        readonly property real first: Math.ceil(plot.yMin/division - 1e-9)
        Repeater {
            model: plot.channelAxis ? Math.min(10, Math.max(0, Math.floor(plot.yMax/channelScale.division + 1e-9)-channelScale.first+1)) : 0
            Text {
                required property int index
                objectName: "viewer.channelAxis.tick." + index
                readonly property real displayValue: (channelScale.first+index)*channelScale.division
                readonly property real rawValue: (displayValue-plot.channelOffset)/plot.channelGain
                width: channelScale.width
                y: (plot.yMax-displayValue)/(plot.yMax-plot.yMin)*channelScale.height-height/2
                horizontalAlignment: Text.AlignRight
                text: root.channelTickText(rawValue, channelScale.division/plot.channelGain)
                font.pixelSize: 12
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 10
                color: plot.channelAxisColor
            }
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
        // Rings on the stored samples nearest the pointer.
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
        objectName: "viewer.capturedTrigger"
        visible: plot.triggerVisible && plot.xMin <= 0 && plot.xMax >= 0
        x: Math.min(view.x + view.plotArea.x - plot.xMin / (plot.xMax - plot.xMin) * view.plotArea.width,
                    view.x + view.plotArea.x + view.plotArea.width - 2)
        y: view.y + view.plotArea.y
        height: view.plotArea.height
        Rectangle { width: 1; height: parent.height; color: "#ed6c02" }
        Text { text: "T"; x: -10; color: "#b34a00"; font.bold: true }
    }
    // The integer model keeps delegates (and pointer grabs) alive while the
    // owner publishes new marker values during a drag.
    Repeater {
        model: plot.channelZeroMarkers.length
        Item {
            id: zero
            required property int index
            width: root.width; height: root.height
            readonly property var info: plot.channelZeroMarkers[index] || {visible: false, value: 0, color: "transparent"}
            visible: !!info.visible
            z: info.selected ? 12 : 11
            readonly property real actualY: root.areaTop + root.markerY(info.value)
            InstrumentHandle {
                id: zeroHandle
                objectName: "viewer.channelZero." + zero.index
                x: root.areaLeft-36
                y: root.areaTop+root.zeroBadgeCenter(zero.index)-height/2
                value: zero.info.value; minimum: plot.yMin; maximum: plot.yMax
                accent: zero.info.color
                label: (zero.index+1) + (value > maximum ? "↑" : value < minimum ? "↓" : "")
                accessibleLabel: "Channel " + (zero.index+1) + " zero level: " + zero.info.name
                help: "CH " + (zero.index+1) + ": raw zero. Drag to move this trace; double-click to reset position."
                resettable: true; highlighted: !!zero.info.selected
                onSelected: plot.selectLegend(zero.index)
                onPreviewed: value => plot.moveChannelZero(zero.index, value)
                onEdited: value => plot.moveChannelZero(zero.index, value)
            }
            Rectangle {
                x: zeroHandle.x+zeroHandle.width; y: zeroHandle.y+zeroHandle.height/2
                readonly property real dx: root.areaLeft-8-x
                readonly property real dy: zero.actualY-y
                width: Math.sqrt(dx*dx+dy*dy); height: 1
                rotation: Math.atan2(dy, dx)*180/Math.PI; transformOrigin: Item.TopLeft
                color: zero.info.color; opacity: 0.7
            }
            Text {
                objectName: "viewer.channelZero.tip." + zero.index
                x: root.areaLeft-10; y: zero.actualY-height/2; text: "▶"; color: zero.info.color; font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent; anchors.margins: -2
                    preventStealing: true; cursorShape: Qt.SizeVerCursor
                    onPressed: mouse => zeroHandle.beginExternal(mapToItem(root, mouse.x, mouse.y))
                    onPositionChanged: mouse => { if (pressed) zeroHandle.moveExternal(mapToItem(root, mouse.x, mouse.y)) }
                    onReleased: zeroHandle.finishExternal()
                    onCanceled: zeroHandle.cancelExternal()
                    onDoubleClicked: zeroHandle.resetPosition()
                    onWheel: wheel => { wheel.accepted = true }
                }
            }
        }
    }
    InstrumentHandle {
        id: levelHandle
        objectName: "viewer.triggerLevel"
        visible: root.triggerHandles && !!plot.instrumentTrigger.levelVisible
        z: 12; width: 34
        x: root.areaLeft+view.plotArea.width+3
        y: root.areaTop+root.markerY(displayedValue)-height/2
        value: plot.instrumentTrigger.level || 0; minimum: plot.yMin; maximum: plot.yMax
        label: "L" + (displayedValue > maximum ? "↑" : displayedValue < minimum ? "↓" : "")
        accessibleLabel: "Trigger level, channel " + (plot.instrumentTrigger.channel+1) + ", raw value " + plot.instrumentTrigger.rawLevel
        help: "CH " + (plot.instrumentTrigger.channel+1) + " trigger: " + ((displayedValue-(plot.instrumentTrigger.offset || 0))/(plot.instrumentTrigger.gain || 1)).toPrecision(5) + " " + (plot.instrumentTrigger.unit || "units") + ". Release to apply and rearm."
        onEdited: value => plot.moveTriggerLevel(value)
    }
    Rectangle {
        visible: levelHandle.dragging
        x: root.areaLeft; y: levelHandle.y+levelHandle.height/2
        width: view.plotArea.width; height: 1; color: levelHandle.accent; opacity: 0.5
    }
    InstrumentHandle {
        id: positionHandle
        objectName: "viewer.triggerPosition"
        visible: root.triggerHandles
        z: 12; width: 46
        x: root.bounded(root.areaLeft+displayedValue/100*view.plotArea.width-width/2, root.areaLeft, root.areaLeft+view.plotArea.width-width)
        y: root.areaTop-height-3
        horizontal: true; minimum: 0; maximum: 100; keyboardStep: 1
        value: plot.instrumentTrigger.pretriggerPercent || 0
        label: "P" + Math.round(displayedValue) + "%"
        accessibleLabel: "Pre-trigger position, " + value + " percent. Applies to the next acquisition."
        help: "Pre-trigger: " + Math.round(displayedValue) + "%. Release to apply to a fresh acquisition. The T line marks the captured event."
        onEdited: value => plot.moveTriggerPosition(value)
    }
    Rectangle {
        visible: positionHandle.dragging
        x: root.areaLeft+positionHandle.displayedValue/100*view.plotArea.width; y: root.areaTop
        width: 1; height: view.plotArea.height; color: positionHandle.accent; opacity: 0.5
    }
    TapHandler {
        enabled: plot.inspectionEnabled && !root.markerDragging
        acceptedButtons: Qt.LeftButton
        onTapped: (eventPoint, button) => {
            root.forceActiveFocus()
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
            if (root.markerDragging) return
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
        enabled: !root.markerDragging
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
        enabled: !root.markerDragging
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
}
