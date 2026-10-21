// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QPixmap>
#include <iostream>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().contains("--check-graphics")) {
        QOpenGLContext context;
        if (!context.create()) { std::cout << "SKIP: no OpenGL context\n"; return 77; }
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        if (!surface.isValid() || !context.makeCurrent(&surface)) {
            std::cout << "SKIP: no usable OpenGL surface\n"; return 77;
        }
        auto* gl = context.functions();
        std::cout << "OpenGL renderer: " << gl->glGetString(GL_RENDERER)
                  << "\nOpenGL version: " << gl->glGetString(GL_VERSION) << '\n';
        return 0;
    }
    if (argc != 2 || !app.primaryScreen()) return 1;
    return app.primaryScreen()->grabWindow(0).save(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
