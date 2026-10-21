// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

import QtQuick
import QtTest

Item {
    id: root
    width: 720; height: 520
    Loader { id: loader; anchors.fill: parent }
    TestCase {
        name: "StudioQuick3DRendering"
        when: windowShown
        function initTestCase() {
            loader.setSource("qrc:/s7studio/viewers/Trajectory3D.qml", {trajectoryView: trajectoryFixture})
            tryCompare(loader, "status", Loader.Ready)
            verify(waitForRendering(loader.item))
        }
        function cleanup() { grabImage(root).save(artifactDirectory + "/trajectory.png") }
        function test_actualTrajectoryPixelsAndCamera() {
            // No 2D labels in this fixture: green pixels can only come from the
            // production Quick3D trajectory Model. A loaded but blank View3D fails.
            var image = grabImage(root)
            var green = 0
            for (var y = 0; y < image.height; y += 2)
                for (var x = 0; x < image.width; x += 2)
                    if (image.green(x, y) > image.red(x, y) + 25
                            && image.green(x, y) > image.blue(x, y) + 25)
                        ++green
            verify(green > 20, "Qt Quick 3D must render actual green trajectory pixels; found " + green)
            var camera = findChild(loader.item, "viewer.camera")
            verify(camera !== null)
            var oldZ = camera.z
            mouseWheel(loader.item, root.width / 2, root.height / 2, 0, 120)
            tryVerify(function() { return camera.z !== oldZ })
            fixture.rotateTrajectory()
            verify(waitForRendering(loader.item))
            var after = grabImage(root)
            verify(!image.equals(after), "camera preset must change rendered geometry")
        }
    }
}
