// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// 3D phase trajectory for the Studio viewer. The trajectory and frame
// geometries, the axis labels and the camera preset come from the C++
// Trajectory3DBridge passed in as `trajectoryView`.
import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

Item {
    id: root
    objectName: "viewer.trajectory"
    required property QtObject trajectoryView

    View3D {
        id: view
        objectName: "viewer.view3d"
        anchors.fill: parent
        camera: camera

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "white"
            // 2x multisampling; previews turn it off (software rasterizers pay per sample).
            antialiasingMode: trajectoryView.antialiased ? SceneEnvironment.MSAA : SceneEnvironment.NoAA
            antialiasingQuality: SceneEnvironment.Medium
        }

        // OrbitCameraController rotates this node and moves the camera;
        // a preset from C++ resets both.
        Node {
            id: orbit
            function applyPreset() {
                orbit.eulerRotation = trajectoryView.viewRotation
                camera.z = 520
            }
            Component.onCompleted: applyPreset()

            PerspectiveCamera {
                id: camera
                objectName: "viewer.camera"
                z: 520
                clipNear: 1
                clipFar: 10000
            }
        }

        Model {
            geometry: trajectoryView.frame
            materials: PrincipledMaterial {
                baseColor: "#9a9a9a"
                lighting: PrincipledMaterial.NoLighting
            }
        }

        Model {
            geometry: trajectoryView.trajectory
            materials: PrincipledMaterial {
                baseColor: "#2ca02c"
                lighting: PrincipledMaterial.NoLighting
                lineWidth: 2
            }
        }

        // The newest point, so the direction of motion is visible.
        Model {
            visible: trajectoryView.hasHead
            source: "#Sphere"
            position: trajectoryView.head
            scale: Qt.vector3d(0.035, 0.035, 0.035)
            materials: PrincipledMaterial {
                baseColor: "#d62728"
                lighting: PrincipledMaterial.NoLighting
            }
        }
    }

    // Axis labels are 2D text placed at their projected 3D anchor points, so
    // they always face the viewer whatever the rotation.
    Repeater {
        model: trajectoryView.labels
        Text {
            required property var modelData
            readonly property vector3d anchor: {
                // Re-evaluated whenever the camera or the view changes.
                orbit.eulerRotation; orbit.rotation; camera.z; camera.position; view.width; view.height
                return view.mapFrom3DScene(modelData.position)
            }
            visible: anchor.z > 0
            x: anchor.x - width / 2
            y: anchor.y - height / 2
            text: modelData.text
            color: modelData.title ? "#202020" : "#505050"
            font.pixelSize: modelData.title ? 13 : 11
            font.bold: modelData.title
        }
    }

    Connections {
        target: trajectoryView
        function onViewRotationChanged() { orbit.applyPreset() }
    }

    OrbitCameraController {
        anchors.fill: parent
        origin: orbit
        camera: camera
    }
}
