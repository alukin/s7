// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "observation/observation_session.hpp"
#include <QBuffer>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace s7studio;
class ObservationSourceTest : public QObject {
    Q_OBJECT
private slots:
    void pagesPreserveIdentityAndPrecisionWithoutWindows() {
        observation::ObservationSource source(71, {3, {}, {}, {}});
        const auto a = source.channel("observer/a");
        const auto b = source.channel("observer/b");
        QCOMPARE(source.channel("observer/a"), a);
        const double precise = std::nextafter(1.0, 2.0);
        for (long i = 0; i < 40000; ++i) QVERIFY(source.append({i % 2 ? b : a, i, i * .0000003, precise + i}));
        QCOMPARE(source.status().samples, std::uint64_t(40000));
        std::uint64_t cursor = 0;
        while (cursor < 40000) {
            const auto page = source.read(cursor, 137);
            QVERIFY(page.error.isEmpty()); QVERIFY(page.samples.size() <= 137);
            for (const auto& value : page.samples) {
                QCOMPARE(value.step, static_cast<std::int64_t>(cursor));
                QCOMPARE(value.channel, cursor % 2 ? b : a);
                QCOMPARE(value.value, precise + cursor); ++cursor;
            }
            QCOMPARE(page.next, cursor);
        }
        QCOMPARE(source.read(0, std::numeric_limits<qsizetype>::max()).samples.size(), 16384);
        QCOMPARE(source.runId(), std::uint64_t(71));
        QBuffer output; QVERIFY(output.open(QIODevice::WriteOnly)); QString error;
        QVERIFY2(source.writeCsv(output, {a}, &error), qPrintable(error));
        const auto rows = output.data().split('\n'); QCOMPARE(rows.size(), 20003);
        QCOMPARE(rows[2].split(',').last().toDouble(), precise);
        QVERIFY(rows[2].startsWith("71,\"observer/a\",0,"));
    }
    void alignedExportUsesRolesAndExactSteps() {
        observation::ObservationSource source(18, {2, {}, {}, {}});
        const auto y = source.channel("p/y"); const auto x = source.channel("p/x");
        QVERIFY(source.append({y, 1, .1, 20})); QVERIFY(source.append({x, 1, .1, 10}));
        QVERIFY(source.append({x, 2, .2, 30})); // incomplete step must not join step 3
        QVERIFY(source.append({y, 3, .3, 60})); QVERIFY(source.append({x, 3, .3, 50}));
        QBuffer output; QVERIFY(output.open(QIODevice::WriteOnly)); QString error;
        QVERIFY2(source.writeAlignedCsv(output, {x, y}, &error), qPrintable(error));
        const auto rows = output.data().split('\n'); QCOMPARE(rows.size(), 5);
        QVERIFY(rows[2].startsWith("18,1,")); QVERIFY(rows[2].endsWith(",10,20"));
        QVERIFY(rows[3].startsWith("18,3,")); QVERIFY(rows[3].endsWith(",50,60"));
    }
    void failedStorageIsExplicitAndMemoryStaysBounded() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        observation::ObservationSource source(4, {2, directory.path() + "/missing", {}, {}});
        const auto id = source.channel("a");
        QVERIFY(source.append({id, 1, .1, 1})); QVERIFY(source.append({id, 2, .2, 2}));
        QVERIFY(!source.append({id, 3, .3, 3}));
        for (long i = 4; i < 10000; ++i) QVERIFY(!source.append({id, i, .4, 4}));
        QCOMPARE(source.status().samples, std::uint64_t(2));
        QVERIFY(source.status().incomplete()); QVERIFY(!source.status().storageError.isEmpty());
        QCOMPARE(source.read(0).samples.size(), 2);
    }
    void flushProviderExceptionBecomesIncompleteStatus() {
        observation::StorageOptions options; options.memoryLimit = 1;
        options.createStorage = []() -> std::unique_ptr<QIODevice> {
            auto buffer = std::make_unique<QBuffer>(); buffer->open(QIODevice::ReadWrite); return buffer;
        };
        options.flushStorage = [](QIODevice&) -> bool { throw std::runtime_error("flush device exception"); };
        observation::ObservationSource source(5, options);
        const auto id = source.channel("a");
        QVERIFY(source.append({id, 1, .1, 1})); QVERIFY(source.append({id, 2, .2, 2}));
        QVERIFY(!source.finish()); QVERIFY(source.status().finished && source.status().incomplete());
        QVERIFY(source.status().storageError.contains("flush device exception"));
    }
    void sessionsRetainOldHandlesAndRejectRetiredBatches() {
        observation::ObservationSession session;
        ScopeSampleBatch first{{{"s/a", 1, .01, 12, 8}}, 3, 8};
        QVERIFY(session.append(first)); auto old = session.source();
        session.beginRun(9);
        QVERIFY(!session.append(first));
        QVERIFY(session.append({{{"s/a", 1, .01, 99, 9}}, 0, 9}));
        QCOMPARE(old->read(0).samples[0].value, 12.0);
        QCOMPARE(old->status().captureDropped, std::uint64_t(3));
        QCOMPARE(session.source()->read(0).samples[0].value, 99.0);
        QCOMPARE(session.source()->runId(), std::uint64_t(9));
        ScopeSampleBatch malformed{{{"s/a", 2, .02, 2, 9}, {"s/a", 3, .03, 3, 8}}, 0, 9};
        QVERIFY(!session.append(malformed)); QCOMPARE(session.source()->status().samples, std::uint64_t(1));
    }
};
QTEST_GUILESS_MAIN(ObservationSourceTest)
#include "observation_source_test.moc"
