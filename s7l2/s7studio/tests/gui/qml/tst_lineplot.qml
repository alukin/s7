import QtQuick
import QtTest

Item {
    id: root
    width: 720; height: 520
    Loader { id: loader; anchors.fill: parent }
    TestCase {
        name: "StudioLinePlot"
        when: windowShown
        property string captureName: "initial"
        function initTestCase() {
            loader.setSource("qrc:/s7studio/viewers/LinePlot.qml", {plot: plotFixture})
            tryCompare(loader, "status", Loader.Ready)
            verify(waitForRendering(loader.item))
        }
        function init() { fixture.resetPlot(); fixture.setLegend(true) }
        function cleanup() {
            // Keep a scene image for every case, including failures; the wrapper
            // also captures the whole virtual screen if the process fails/crashes.
            grabImage(root).save(artifactDirectory + "/lineplot-" + captureName + ".png")
        }
        function test_titlesAndLegend() {
            captureName = "titlesAndLegend"
            verify(waitForRendering(loader.item))
            var pixels = grabImage(root)
            var red = 0
            for (var y = 0; y < pixels.height; y += 2)
                for (var x = 0; x < pixels.width; x += 2)
                    if (pixels.red(x, y) > pixels.green(x, y) + 40
                            && pixels.red(x, y) > pixels.blue(x, y) + 40)
                        ++red
            verify(red > 50, "Qt Graphs must render the reference curve, not only its legend")
            compare(plotFixture.xTitle, "Model time (s)")
            compare(plotFixture.yTitle, "Value")
            var legend = findChild(loader.item, "viewer.legend")
            verify(legend !== null)
            tryCompare(legend, "visible", true)
            fixture.setLegend(false)
            tryCompare(legend, "visible", false)
        }
        function test_wheelZoomAndDragPan() {
            captureName = "wheelZoomAndDragPan"
            var plot = loader.item
            var originalSpan = plotFixture.xMax - plotFixture.xMin
            mouseWheel(plot, plot.width / 2, plot.height / 2, 0, 120)
            tryVerify(function() { return plotFixture.xMax - plotFixture.xMin < originalSpan })
            var before = plotFixture.xMin
            mouseDrag(plot, plot.width / 2, plot.height / 2, plot.width / 8, plot.height / 10)
            tryVerify(function() { return plotFixture.xMin !== before })
        }
    }
}
