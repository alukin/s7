#include "lifecycle_test_support.hpp"
#include "run_provenance.hpp"
#include "observation/scope_results.hpp"
#include "observation/run_metadata.hpp"
#include "diagram_harness.hpp"
#include <QBuffer>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <type_traits>

extern "C" void s7_register_elements(void*);
using namespace s7studio;
namespace {
QByteArray savedSource(const observation::ObservationSource& source) {
    QVector<observation::ChannelId> channels;
    for (const auto& channel : source.channels()) channels.push_back(channel.id);
    QString error;
    auto projection = observation::scopeProjection(source, "scope", channels, &error);
    if (!projection) throw std::runtime_error(error.toStdString());
    QBuffer out; out.open(QIODevice::WriteOnly);
    if (!projection->writeRunResults(out, &error)) throw std::runtime_error(error.toStdString());
    return out.data();
}
QJsonObject importedProvenance(const QByteArray& bytes) {
    QBuffer in; in.setData(bytes); in.open(QIODevice::ReadOnly);
    ui::ScopeRecording recording("import"); QString error;
    if (!recording.openRunResults(in, "saved", &error)) throw std::runtime_error(error.toStdString());
    return recording.series().first().sourceMetadata.value("provenance").toObject();
}
boost::json::value noiseModel(const char* type, int seed) {
    return boost::json::object{{"name", "seeded_noise"}, {"dt", .001},
        {"elements", boost::json::array{boost::json::object{{"id", "n"}, {"type", type}, {"params", {{"seed", seed}}}}}},
        {"connections", boost::json::array{}}};
}
}
class RunProvenanceTest : public QObject {
    Q_OBJECT
    std::shared_ptr<Gate> gate_;
    std::unique_ptr<LocalSchemaRunner> runner_;
private slots:
    void initTestCase() { registerLifecycleElements(); s7_register_elements(&s7::ElementRegistry::instance()); }
    void init() { gate_ = currentGate = std::make_shared<Gate>(); runner_ = std::make_unique<LocalSchemaRunner>(); }
    void cleanup() { gate_->releaseAll(); runner_->reset(); runner_.reset(); }
    void metadataSurvivesEditsAndReruns() {
        auto model = schema(2.5);
        runner_->run(model, .001, 1, {{"scope/value", "source", 0}});
        QTRY_COMPARE(gate_->entered.load(), 1);
        const auto source = runner_->recordedSource();
        const auto original = source->provenance(); QVERIFY(!original.isEmpty());
        auto copy = source->provenance(); copy["model_name"] = "wrong";
        QVERIFY(copy != source->provenance());
        model.as_object().at("elements").as_array()[0].as_object()["params"].as_object()["value"] = 99.;
        static_assert(std::is_const_v<std::remove_reference_t<decltype(*source)>>);
        observation::ObservationSource owner(55); owner.setProvenance(original);
        QVERIFY_EXCEPTION_THROWN(owner.setProvenance(copy), std::logic_error);
        QSignalSpy completed(runner_.get(), &LocalSchemaRunner::runCompleted);
        gate_->allow(); QTRY_COMPARE(completed.count(), 1);
        const auto bytes = savedSource(*source);
        QVERIFY(importedProvenance(bytes) == original);
        auto replay = configurationFromProvenance(importedProvenance(bytes));
        QCOMPARE(replay.schemaJson.as_object().at("elements").as_array()[0].as_object().at("params").as_object().at("value").to_number<double>(), 2.5);
        gate_->releaseAll();
        runner_->run(model, .002, 1, {{"scope/value", "source", 0}});
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(source->provenance() == original);
        QVERIFY(runner_->recordedSource()->provenance().value("run_uuid") != original.value("run_uuid"));
        QVERIFY(runner_->recordedSource()->provenance().value("model_sha256") != original.value("model_sha256"));
        runner_->reset(); QVERIFY(source->provenance() == original);
        QBuffer csv; csv.open(QIODevice::WriteOnly); QString error;
        QVERIFY(source->writeCsv(csv, {}, &error));
        const auto first = csv.data().split('\n').front();
        const auto csvMetadata = QJsonDocument::fromJson(first.mid(QByteArray("# s7_observation ").size())).object();
        QVERIFY(csvMetadata.value("provenance").toObject() == original);
        const auto software = original.value("software").toObject();
        QVERIFY(!software.value("project_version").toString().isEmpty());
        QVERIFY(!software.value("revision").toString().isEmpty());
        QVERIFY(!software.value("compiler").toString().isEmpty());
    }
    void defaultsNestedDefinitionsAndUint64AreExact() {
        s7::ElementMetadata metadata;
        metadata.type_name = "ProvenanceNested"; metadata.version = "test-1";
        metadata.schema_definition = boost::json::object{{"name", "nested"}, {"dt", .001},
            {"elements", boost::json::array{boost::json::object{{"id", "n"}, {"type", "WhiteNoiseGenerator"}}}},
            {"connections", boost::json::array{}}, {"exposed_outputs", boost::json::array{
                boost::json::object{{"name", "out"}, {"type", "F64"}, {"element", "n"}, {"port", 0}}}}};
        s7::ElementRegistry::instance().register_element(s7::ElementDescriptor("ProvenanceNested",
            [](const s7::ElementCreationContext&) -> std::unique_ptr<s7::Element> { throw std::runtime_error("Unfrozen registry definition was used"); }, metadata));
        auto model = noiseModel("ProvenanceNested", 0);
        model.as_object().at("elements").as_array()[0].as_object().erase("params");
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        model.as_object().at("elements").as_array().push_back(boost::json::object{
            {"id", "large"}, {"type", "Constant"}, {"params", {{"type", "U64"}, {"value", maximum}}}});
        auto prepared = prepareRunProvenance({77, model, .001, 8, {{"scope/v", "n", 0}}});
        const auto effective = boost::json::parse(prepared.provenance.value("effective_model_json").toString().toStdString());
        const auto& elements = effective.as_object().at("elements").as_array();
        const auto& nested = elements[0].as_object().at("schema").as_object().at("elements").as_array()[0].as_object().at("params").as_object();
        QCOMPARE(nested.at("amplitude").to_number<double>(), 1.);
        QVERIFY(nested.at("seed").to_number<int>() > 0);
        QCOMPARE(elements[1].as_object().at("params").as_object().at("value").to_number<std::uint64_t>(), maximum);
        const auto seeds = prepared.provenance.value("seeds").toArray();
        QCOMPARE(seeds.size(), 1); QVERIFY(seeds[0].toObject().value("generated").toBool());
        QCOMPARE(seeds[0].toObject().value("path").toString(), QString("n/n"));
        observation::ObservationSource source(77); source.setProvenance(prepared.provenance);
        source.channel("scope/v"); QVERIFY(source.append({0, 1, .001, 1})); QVERIFY(source.finish());
        const auto restored = configurationFromProvenance(importedProvenance(savedSource(source)));
        QCOMPARE(restored.schemaJson.as_object().at("elements").as_array()[1].as_object().at("params").as_object().at("value").to_number<std::uint64_t>(), maximum);
        // The embedded definition, including the resolved seed, is enough even
        // if the library descriptor is removed after saving.
        s7::ElementRegistry::instance().unregister({"ProvenanceNested"});
        QSignalSpy done(runner_.get(), &LocalSchemaRunner::runCompleted);
        runner_->run(restored.schemaJson, restored.dt, restored.ticks, {{"scope/v", "n", 0}});
        QTRY_COMPARE(done.count(), 1); QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
    }
    void savedConfigurationReproducesSamples_data() {
        QTest::addColumn<QString>("file"); QTest::addColumn<QString>("noise"); QTest::addColumn<int>("seed");
        QTest::newRow("linear") << QString("s7tests/diagrams/euler_scalar_decay.s7diagram") << QString() << 0;
        QTest::newRow("nonlinear") << QString("examples/pendulum/nonlinear_pendulum_euler.s7diagram") << QString() << 0;
        QTest::newRow("white-random-seed") << QString() << QString("WhiteNoiseGenerator") << 0;
        QTest::newRow("pink-random-seed") << QString() << QString("PinkNoiseGenerator") << 0;
        QTest::newRow("explicit-seed") << QString() << QString("WhiteNoiseGenerator") << 123;
    }
    void savedConfigurationReproducesSamples() {
        QFETCH(QString, file); QFETCH(QString, noise); QFETCH(int, seed);
        boost::json::value model; double dt = .001;
        std::vector<RunProbe> probes;
        if (noise.isEmpty()) {
            const auto root = s7tests::read_json_object(std::filesystem::path(S7_SOURCE_DIR) / file.toStdString());
            model = root.at("schema"); dt = root.at("dt").to_number<double>();
            probes = {{"scope/v", file.contains("scalar") ? "x" : "theta", 0}};
            if (!file.contains("scalar")) probes.push_back({"scope/omega", "omega", 0});
        } else { model = noiseModel(noise.toUtf8().constData(), seed); probes = {{"scope/v", "n", 0}}; }
        QSignalSpy done(runner_.get(), &LocalSchemaRunner::runCompleted);
        runner_->run(model, dt, 256, probes);
        QTRY_COMPARE(done.count(), 1); QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
        const auto source = runner_->recordedSource();
        const auto bytes = savedSource(*source);
        const auto provenance = importedProvenance(bytes);
        const auto restored = configurationFromProvenance(provenance);
        // Deliberately discard the editor request; reconstruction uses only
        // metadata read from the saved binary result.
        model = boost::json::object{};
        runner_->reset();
        runner_->run(restored.schemaJson, restored.dt, restored.ticks, restored.probes);
        QTRY_COMPARE(done.count(), 2); QVERIFY2(done[1][0].toBool(), qPrintable(done[1][1].toString()));
        const auto replay = runner_->recordedSource();
        QCOMPARE(replay->status().samples, source->status().samples);
        const auto a = source->read(0).samples, b = replay->read(0).samples;
        QCOMPARE(a.size(), static_cast<qsizetype>(256 * restored.probes.size())); QCOMPARE(a.size(), b.size());
        for (qsizetype i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].channel, b[i].channel); QCOMPARE(a[i].step, b[i].step); QVERIFY(a[i].timeSeconds == b[i].timeSeconds); QVERIFY(a[i].value == b[i].value);
        }
        QCOMPARE(replay->provenance().value("model_sha256"), provenance.value("model_sha256"));
        QVERIFY(replay->provenance().value("run_uuid") != provenance.value("run_uuid"));
        if (!noise.isEmpty()) {
            const auto captured = provenance.value("seeds").toArray()[0].toObject();
            QCOMPARE(captured.value("generated").toBool(), seed == 0);
            QVERIFY(captured.value("value_json").toString().toInt() > 0);
        }
    }
    void headerLimitsAndCorruptProvenanceAreTransactional() {
        auto prepared = prepareRunProvenance({9, schema(), .001, 1, {{"scope/v", "source", 0}}});
        // Model annotations are included in identity and can legitimately
        // exceed the previous 64 KiB result-header limit.
        auto effective = prepared.effectiveSchema;
        effective.as_object()["note"] = std::string(70000, 'x');
        prepared = prepareRunProvenance({9, effective, .001, 1, {{"scope/v", "source", 0}}});
        observation::ObservationSource source(9); source.setProvenance(prepared.provenance);
        source.channel("scope/v"); QVERIFY(source.append({0, 1, .001, 2.})); QVERIFY(source.finish());
        auto bytes = savedSource(source); QVERIFY(bytes.size() > 65536);
        QVERIFY(importedProvenance(bytes) == prepared.provenance);
        ui::ScopeRecording existing("existing"); existing.appendSample("old", .1, 99.);
        const auto initialCount = existing.series().size();
        // Replace one digest character without changing the length/payload.
        const auto digest = prepared.provenance.value("model_sha256").toString().toUtf8();
        const auto at = bytes.indexOf(digest); QVERIFY(at > 0); bytes[at] = digest[0] == 'a' ? 'b' : 'a';
        QBuffer in(&bytes); in.open(QIODevice::ReadOnly); QString error;
        QVERIFY(!existing.openRunResults(in, "corrupt", &error));
        QVERIFY(error.contains("fingerprint")); QCOMPARE(existing.series().size(), initialCount);
        QCOMPARE(existing.series().first().lastValue, 99.);
        QByteArray huge;
        QBuffer header(&huge); header.open(QIODevice::WriteOnly);
        QDataStream out(&header); out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("S7SCOPE\n", 8); out << ui::ScopeRecording::kResultVersion << (ui::ScopeRecording::kMaxResultHeaderBytes + 1);
        header.close(); QBuffer oversized(&huge); oversized.open(QIODevice::ReadOnly);
        QVERIFY(!existing.openRunResults(oversized, "oversized", &error));
        QCOMPARE(existing.series().size(), initialCount);
        ui::ScopeRecording tooLarge("large"); tooLarge.appendSample("v", .001, 1);
        tooLarge.setRunMetadata({{"note", QString(ui::ScopeRecording::kMaxResultHeaderBytes, 'x')}});
        QBuffer destination; destination.open(QIODevice::WriteOnly);
        QVERIFY(!tooLarge.writeRunResults(destination, &error));
        QVERIFY(error.contains("16 MiB")); QVERIFY(destination.data().isEmpty());
        QByteArray oldVersion;
        QBuffer oldHeader(&oldVersion); oldHeader.open(QIODevice::WriteOnly);
        QDataStream old(&oldHeader); old.setByteOrder(QDataStream::LittleEndian);
        old.writeRawData("S7SCOPE\n", 8); old << quint32(1) << quint32(0);
        oldHeader.close(); QBuffer unsupported(&oldVersion); unsupported.open(QIODevice::ReadOnly);
        QVERIFY(!existing.openRunResults(unsupported, "old version", &error));
        QCOMPARE(existing.series().size(), initialCount);
        auto corrupt = prepared.provenance; corrupt["rate"] = QJsonObject{{"numerator", "2"}, {"denominator", "1"}};
        QVERIFY_EXCEPTION_THROWN(static_cast<void>(configurationFromProvenance(corrupt)), std::invalid_argument);
    }
    void newPresentationDoesNotInheritOldRunMetadata() {
        ui::ScopeRecording recording("scope");
        recording.setRunMetadata({{"run_id", "17"}}); recording.appendSample("v", .001, 1.);
        recording.archiveLive("old");
        QVERIFY(recording.runMetadata().isEmpty());
        QCOMPARE(recording.series().first().sourceMetadata.value("run_id").toString(), QString("17"));
        recording.setRunMetadata({{"run_id", "18"}}); recording.appendSample("v", .001, 2.);
        recording.clearLive();
        QVERIFY(recording.runMetadata().isEmpty());
        QCOMPARE(recording.series().first().sourceMetadata.value("run_id").toString(), QString("17"));
    }
};
QTEST_GUILESS_MAIN(RunProvenanceTest)
#include "run_provenance_test.moc"
