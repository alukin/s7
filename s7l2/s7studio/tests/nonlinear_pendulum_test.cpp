#include "nonlinear_pendulum_cases.hpp"
#include "local_schema_runner.hpp"
#include "scope_view_window.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "diagram_document.hpp"
#include "scene_serializer.hpp"

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char** argv) try {
    QApplication app(argc,argv);
    QTemporaryDir directory;
    if (!directory.isValid()) throw std::runtime_error("Cannot create test recording directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    QApplication::setOrganizationName(QStringLiteral("s7-test"));
    QApplication::setApplicationName(QStringLiteral("nonlinear-pendulum"));
    QSettings{}.setValue(QStringLiteral("scope/temporaryDirectory"),directory.path());
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    namespace p=s7tests::nonlinear_pendulum;
    const std::filesystem::path report_dir(S7_SINGLE_PENDULUM_STUDIO_REPORT_DIR);
    std::filesystem::create_directories(report_dir);
    std::ofstream summary(report_dir/"recording_summary.csv");
    if (!summary) throw std::runtime_error("Cannot create recording report");
    summary<<"method,frequency_hz,duration_s,runtime_samples,csv_rows,preview_dropped,max_runtime_difference,csv_round_trip\n";
    for (const auto method:p::methods) {
        for (const int frequency:p::frequencies) {
            const auto root=p::diagram(method,frequency);
            const auto document=s7studio::diagram::DiagramDocument::fromJson(root);
            s7studio::diagram::DiagramScene scene;
            s7studio::diagram::SceneSerializer{}.deserialize(document,scene);
            const s7studio::diagram::NetModel net(scene);
            if (!net.problems().empty() || net.observationConnections().size()!=2) {
                throw std::runtime_error("Nonlinear diagram observer wiring failed to load");
            }
            const auto& saved=root.at("probes").as_array();
            for (const auto& wire:net.observationConnections()) {
                const auto& probe=saved.at(wire.to.port).as_object();
                if (wire.to.element!="scope_states" || wire.from.element!=std::string(probe.at("element").as_string()) ||
                    wire.from.port!=probe.at("port").to_number<int>()) throw std::runtime_error("Scope/probe mapping mismatch");
            }
            const auto raw_path=std::filesystem::path(directory.path().toStdString())/"reference.bin";
            {
                std::ofstream raw(raw_path,std::ios::binary);
                if (!raw) throw std::runtime_error("Cannot create reference stream");
                static_cast<void>(p::run(method,frequency,&raw));
            }
            std::ifstream expected(raw_path,std::ios::binary);
            std::array<double,3> row{};
            const auto read_expected=[&] {
                if (!expected.read(reinterpret_cast<char*>(row.data()),sizeof(row))) throw std::runtime_error("Reference trace ended early");
            };
            std::vector<s7studio::RunProbe> probes;
            for (const auto& value:saved) {
                const auto& probe=value.as_object();
                probes.push_back({std::string(probe.at("name").as_string()),std::string(probe.at("element").as_string()),
                                  probe.at("port").to_number<int>()});
            }
            s7studio::ui::ScopeViewWindow scope(QStringLiteral("pendulum"));
            s7studio::LocalSchemaRunner runner;
            bool completed=false,success=false;
            QString error;
            QObject::connect(&runner,&s7studio::LocalSchemaRunner::runCompleted,&runner,
                [&](bool ok,const QString& message) { completed=true;success=ok;error=message; });
            std::size_t received=0;
            double max_difference=0;
            const double tolerance=p::budgets().at("bundled_headless_max_difference").to_number<double>();
            std::uint64_t previewDropped = 0;
            // Preview is intentionally lossy. Verify the worker's authoritative
            // source, even when delivery is withheld for the entire run.
            const auto consume=[&] {
                previewDropped += runner.takeScopeSamples().dropped;
            };
            const long steps=static_cast<long>(p::duration*frequency);
            runner.run(document.runtimeSchema,1.0/frequency,steps,probes);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
            while (!completed && std::chrono::steady_clock::now()<deadline) {
                app.processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            consume();
            const auto source = runner.recordedSource();
            if (!completed || !success || !source || !source->status().finished || source->status().incomplete()
                || source->status().samples != static_cast<std::size_t>(steps)*2) {
                throw std::runtime_error("Bundled run incomplete: "+error.toStdString());
            }
            const auto expectedDrops = std::max(std::int64_t(0), std::int64_t(steps)*2-200000);
            if (previewDropped != static_cast<std::uint64_t>(expectedDrops))
                throw std::runtime_error("Withheld preview did not account for every skipped sample");
            const auto channels = source->channels();
            for (std::uint64_t cursor = 0; cursor < source->status().samples;) {
                const auto page = source->read(cursor);
                if (!page.error.isEmpty() || page.next <= cursor) throw std::runtime_error("Cannot read authoritative capture");
                for (const auto& sample : page.samples) {
                    const auto channel = received % 2;
                    if (channel == 0) read_expected();
                    if (sample.channel != channels.at(channel).id || sample.step != static_cast<long>(received/2+1)
                        || sample.timeSeconds != row[0] || !std::isfinite(sample.value))
                        throw std::runtime_error("Bundled recording sample/time/channel mismatch");
                    max_difference = std::max(max_difference, std::abs(sample.value-row[channel+1]));
                    if (max_difference > tolerance) throw std::runtime_error("Bundled/headless numerical difference");
                    ++received;
                }
                cursor = page.next;
            }
            QVector<s7studio::viewers::InputBinding> bindings;
            for (const auto& channel : source->channels()) bindings.push_back({channel.name, channel.id});
            scope.attachSource(source, bindings);
            runner.reset(); // the source and export survive worker reset
            QTemporaryFile csv(directory.path()+QStringLiteral("/export-XXXXXX.csv"));
            if (!csv.open() || !scope.writeRecordedCsv(csv,&error) || !csv.flush() || !csv.seek(0)) {
                throw std::runtime_error("Scope CSV export failed: "+error.toStdString());
            }
            QTextStream input(&csv);
            if (input.readLine()!=QStringLiteral("scope,channel,time_s,value")) throw std::runtime_error("CSV header mismatch");
            std::array<std::size_t,2> counts{};
            QString last_channel;
            std::size_t rows=0;
            while (!input.atEnd()) {
                const auto columns=input.readLine().split(QLatin1Char(','));
                if (columns.size()!=4 || columns[0]!=QStringLiteral("pendulum")) throw std::runtime_error("Malformed Scope CSV");
                const int channel=columns[1]==QStringLiteral("theta")?0:columns[1]==QStringLiteral("omega")?1:-1;
                if (channel<0) throw std::runtime_error("Unexpected CSV channel");
                if (last_channel!=columns[1]) {
                    if (counts[channel]) throw std::runtime_error("CSV channel order changed mid-recording");
                    expected.clear();expected.seekg(0);last_channel=columns[1];
                }
                read_expected();
                bool time_ok=false,value_ok=false;
                const double time=columns[2].toDouble(&time_ok),value=columns[3].toDouble(&value_ok);
                if (!time_ok || !value_ok || time!=row[0] || value!=row[channel+1]) {
                    throw std::runtime_error("Scope CSV lost timestamp/value precision at row "+std::to_string(rows));
                }
                ++counts[channel];++rows;
            }
            if (counts[0]!=static_cast<std::size_t>(steps) || counts[1]!=counts[0]) throw std::runtime_error("Incomplete full-resolution CSV");
            summary<<method<<','<<frequency<<','<<p::duration<<','<<received<<','<<rows<<','<<previewDropped<<','<<max_difference<<",EXACT\n";
            summary.flush();
            std::cout<<method<<' '<<frequency<<" Hz: "<<received<<" captured samples; "<<rows
                     <<" CSV rows, exact round trip; maximum runtime difference="<<max_difference<<std::endl;
        }
    }
    return 0;
} catch (const std::exception& ex) { std::cerr<<ex.what()<<'\n'; return 1; }
