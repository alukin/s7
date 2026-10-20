#include "main_window.hpp"
#include "atomic_file.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <thread>
#include <QTimer>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class Window : public s7studio::MainWindow {
public:
    Window() : MainWindow(nullptr, false) {}
    using MainWindow::currentDocument;
    using MainWindow::documentModified;
};
auto scene(Window& window) -> s7studio::diagram::DiagramScene* {
    return window.findChild<s7studio::diagram::DiagramEditor*>()->scene();
}
void edit(Window& window) {
    s7studio::diagram::ElementNode::Element element;
    element.id = "added"; element.type = "Constant"; element.params = {{"value", 42.0}};
    scene(window)->addElement(element, {40, 40});
    require(window.documentModified(), "placement makes document dirty");
}
void choose(QMessageBox::StandardButton button) {
    QTimer::singleShot(0, [button] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(box != nullptr, "expected unsaved dialog");
        box->button(button)->click();
    });
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        QCoreApplication::setOrganizationName("s7-test");
        QCoreApplication::setApplicationName("document-safety");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        const auto input = dir.filePath("input.s7diagram");
        QString error;
        const QByteArray bytes(R"({"document_kind":"s7.diagram","dt":0.001,"schema":{"name":"safe","elements":[],"connections":[]},"studio_run":{"runtime_seconds":123,"clear_scope_traces":false}})");
        require(s7studio::writeFileAtomically(input, bytes, error), "write fixture");
        Window window;
        window.openPath(input);
        require(!window.documentModified(), "opened document clean");
        const auto original = window.currentDocument();
        require(original.as_object().at("studio_run").as_object().at("runtime_seconds").as_int64() == 123, "run duration restored");
        edit(window);
        const auto edited = window.currentDocument();
        choose(QMessageBox::Cancel);
        window.openPath(input);
        require(window.currentDocument() == edited, "cancel open preserves edits");
        choose(QMessageBox::Cancel);
        require(!window.close(), "cancel prevents close");
        require(window.documentModified(), "cancel preserves dirty state");
        choose(QMessageBox::Discard);
        window.openPath(input);
        require(window.currentDocument() == original && !window.documentModified(), "discard opens clean document");
        edit(window);
        scene(window)->undoStack()->undo();
        require(!window.documentModified(), "undo restores clean snapshot");
        edit(window);
        choose(QMessageBox::Save);
        window.openPath(input);
        require(window.currentDocument() == edited && !window.documentModified(), "save then reopen preserves edits");
        // Remove an element through undo, then persist using close's Save choice.
        scene(window)->undoStack()->clear();
        s7studio::diagram::ElementNode::Element extra;
        extra.id = "second"; extra.type = "Constant";
        scene(window)->addElement(extra, {80,80});
        choose(QMessageBox::Save);
        require(window.close(), "save permits close");
        Window reopened;
        reopened.openPath(input);
        require(scene(reopened)->elementNode("second") != nullptr, "close-save round trip");
        require(!reopened.documentModified(), "reopened clean");
        auto* runner = reopened.findChild<s7studio::LocalSchemaRunner*>();
        require(runner != nullptr, "embedded runner");
        for (bool paused : {false, true}) {
            runner->run(reopened.currentDocument().as_object().at("schema"), .001, -1, {});
            require(runner->isRunning(), "continuous worker starts");
            if (paused) { runner->pause(); require(runner->state() == s7studio::RunState::Pausing || runner->isPaused(), "pause requested"); }
            reopened.openPath(input);
            require(!runner->isRunning() && !runner->isPaused(), "replacement stops and joins old run");
            require(runner->takeScopeSamples().samples.empty(), "replacement clears old mailbox");
        }
        // Queue worker notifications deterministically without processing GUI
        // events. They must be rejected after a document changes generation.
        std::thread retiring([runner] {
            emit runner->logMessage(QStringLiteral("RETIRED_RUN_SENTINEL"));
            emit runner->runCompleted(false, QStringLiteral("RETIRED_RUN_SENTINEL"));
            emit runner->ticksProgress(987654321, 987654321);
            emit runner->scopeSamplesAvailable();
        });
        retiring.join();
        reopened.openPath(input);
        QCoreApplication::sendPostedEvents();
        for (auto* output : reopened.findChildren<QPlainTextEdit*>())
            require(!output->toPlainText().contains("RETIRED_RUN_SENTINEL"), "retired callbacks isolated");
        const auto other = dir.filePath("other.s7diagram");
        require(s7studio::writeFileAtomically(other, bytes, error), "other fixture");
        s7studio::diagram::ElementNode::Element unsaved;
        unsaved.id = "unsaved"; unsaved.type = "Constant";
        scene(reopened)->addElement(unsaved, {120,120});
        const auto pending = reopened.currentDocument();
        // Turn the existing document path into a directory so Save fails even
        // under root; the original file is preserved alongside it.
        require(QFile::rename(input, input + ".original"), "preserve original fixture");
        require(QDir{}.mkdir(input), "inject invalid save destination");
        QTimer failureResponder;
        QObject::connect(&failureResponder, &QTimer::timeout, [] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                if (box->windowTitle() == "Unsaved changes") box->button(QMessageBox::Save)->click();
                else if (box->windowTitle() == "Save failed") box->button(QMessageBox::Ok)->click();
            }
        });
        failureResponder.start(0);
        reopened.openPath(other);
        failureResponder.stop();
        require(reopened.documentModified() && reopened.currentDocument() == pending,
                "failed Save cancels replacement and retains edits");
        choose(QMessageBox::Discard);
        require(reopened.close(), "discard permits close");
        return 0;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
}
