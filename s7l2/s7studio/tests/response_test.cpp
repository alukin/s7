// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "local_runtime_v2.hpp"
#include "viewers/response_window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <fstream>
#include <iostream>
#include <s7/core.hpp>
#include <stdexcept>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        for (const std::string name : {"gain", "delay", "rc"}) {
            std::ifstream in(std::string(S7_RESPONSE_DIR) + "/" + name + ".s7diagram");
            const std::string text((std::istreambuf_iterator<char>(in)), {});
            auto schema = s7studio::runtime::buildSchema(boost::json::parse(text).as_object().at("schema"));
            schema.set_rate(s7::Rate::from_hz(1000));
            s7::NodeRuntime runtime(s7::Compiler::compile(schema));
            auto source = std::make_shared<s7studio::observation::ObservationSource>(1);
            source->channel("response/input");
            source->channel("response/output");
            for (int n = 1; n <= 2000; ++n) {
                runtime.step();
                source->append({0, n, double(n) / 1000, runtime.real_output_of("excitation")});
                source->append(
                    {1, n, double(n) / 1000, runtime.real_output_of(name == "delay" ? "delay2" : "plant")});
            }
            if (!source->finish())
                throw std::runtime_error("Capture flush");
            s7studio::viewers::ResponseWindow viewer;
            viewer.attachSource(source, {{"input", 0}, {"output", 1}});
            while (viewer.refreshSource()) {
            }
            viewer.analyze();
            QElapsedTimer timer;
            timer.start();
            while (!viewer.hasResult() && viewer.error().isEmpty() && timer.elapsed() < 5000) {
                viewer.poll();
                QApplication::processEvents();
                QThread::msleep(5);
            }
            if (!viewer.hasResult())
                throw std::runtime_error(viewer.error().isEmpty() ? "Response timeout"
                                                                  : viewer.error().toStdString());
            QTemporaryDir files;
            const auto path = files.filePath("response.s7analysis");
            viewer.saveResult(path);
            viewer.clearSamples();
            viewer.openResult(path);
            if (!viewer.hasResult())
                throw std::runtime_error("Response result round-trip");
            viewer.clearSamples();
            if (viewer.hasResult())
                throw std::runtime_error("Old response survived clear");
            std::cout << name << " capture and asynchronous response passed\n";
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
