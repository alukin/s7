#include "coupled_pendulum_cases.hpp"
#include "local_schema_runner.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "diagram_document.hpp"
#include "scene_serializer.hpp"

#include <QApplication>

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) try
{
    QApplication app(argc, argv);
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    using namespace s7tests::pendulum;
    for (const auto method : methods) {
        for (const auto mode : modes) {
            // Exercise the actual Studio document/scene loader and observer
            // wiring. No network service or state directory is involved.
            auto root = diagram(method, mode, 0.001);
            s7studio::diagram::DiagramScene scene;
            const auto document = s7studio::diagram::DiagramDocument::fromJson(root);
            s7studio::diagram::SceneSerializer{}.deserialize(document, scene);
            const s7studio::diagram::NetModel net(scene);
            if (!net.problems().empty() || net.observationConnections().size() != 4 || scene.probes().size() != 4) {
                throw std::runtime_error(name(method, mode) + ": diagram wiring/probes failed to load");
            }
            const auto& saved_probes = root.at("probes").as_array();
            for (const auto& connection : net.observationConnections()) {
                const auto& probe = saved_probes.at(connection.to.port).as_object();
                if (connection.to.element != "scope_states" ||
                    connection.from.element != std::string(probe.at("element").as_string()) ||
                    connection.from.port != probe.at("port").to_number<int>()) {
                    throw std::runtime_error("Scope channel does not match the numerical probe");
                }
            }
            const auto roundtrip = s7studio::diagram::SceneSerializer{}.serialize(scene, {});
            if (roundtrip.runtimeSchema.as_object().at("elements").as_array().size() !=
                root.at("schema").as_object().at("elements").as_array().size()) {
                throw std::runtime_error("Diagram round trip changed functional elements");
            }
            for (const int frequency : frequencies) {
                const double dt = 1.0 / frequency;
                const auto expected = run_headless(method, mode, frequency);
                root = diagram(method, mode, dt);
                std::vector<s7studio::RunProbe> probes;
                for (const auto& value : root.at("probes").as_array()) {
                    const auto& probe = value.as_object();
                    probes.push_back({std::string(probe.at("name").as_string()),
                                      std::string(probe.at("element").as_string()),
                                      probe.at("port").to_number<int>()});
                }
                s7studio::LocalSchemaRunner runner;
                bool completed = false;
                bool success = false;
                QString error;
                QObject::connect(&runner, &s7studio::LocalSchemaRunner::runCompleted, &runner,
                    [&](bool ok, const QString& message) { completed = true; success = ok; error = message; });
                std::size_t received = 0, checkpoint = 0;
                double max_difference = 0;
                const auto consume = [&] {
                    const auto batch = runner.takeScopeSamples();
                    if (batch.dropped != 0) throw std::runtime_error("Bundled runtime lost samples during numerical test");
                    for (const auto& sample : batch.samples) {
                        const long step = static_cast<long>(received / 4 + 1);
                        const auto channel = received % 4;
                        if (!std::isfinite(sample.value) || sample.step != step || sample.probeName != probes[channel].name ||
                            std::abs(sample.timeSeconds - step * dt) > 1e-10) {
                            throw std::runtime_error("Bundled runtime sample order/time/channel mismatch");
                        }
                        if (checkpoint < expected.checkpoints.size() && step == expected.checkpoints[checkpoint].step) {
                            max_difference = std::max(max_difference,
                                std::abs(sample.value - expected.checkpoints[checkpoint].value[channel]));
                            if (channel == 3) ++checkpoint;
                        }
                        ++received;
                    }
                };
                const long ticks = static_cast<long>(duration * frequency);
                runner.run(root.at("schema"), dt, ticks, probes);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
                while (!completed && std::chrono::steady_clock::now() < deadline) {
                    app.processEvents();
                    consume();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                consume();
                runner.reset(); // joins worker, including on failed completion
                if (!completed || !success) throw std::runtime_error("Bundled run failed: " + error.toStdString());
                if (received != static_cast<std::size_t>(ticks) * 4 || checkpoint != expected.checkpoints.size()
                    || max_difference > 1e-12) {
                    throw std::runtime_error(name(method, mode) + ": bundled/headless traces disagree");
                }
                std::cout << name(method, mode) << ' ' << frequency << " Hz: " << received
                          << " samples; bundled/headless maximum difference=" << max_difference << std::endl;
            }
        }
    }
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}
