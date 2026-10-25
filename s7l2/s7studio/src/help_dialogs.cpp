// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "help_dialogs.hpp"
#include "s7studio_build_identity.hpp"

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace s7studio {
namespace {
struct Topic { const char* title; const char* path; };
constexpr Topic licensing{"Licensing", "qrc:/s7studio/legal/LICENSING.md"};
constexpr Topic thirdPartyNotices{"Third-party notices", "qrc:/s7studio/legal/THIRD_PARTY_NOTICES.md"};
constexpr Topic topics[] = {
    {"Getting started", "qrc:/s7studio/help/docs/STUDIO_USER_GUIDE.md"},
    {"Live oscilloscope prototype", "qrc:/s7studio/help/docs/LIVE_OSCILLOSCOPE_PROTOTYPE.md"},
    {"Live FFT spectrum", "qrc:/s7studio/help/docs/STUDIO_SPECTRUM.md"},
    {"Live constellation and correlation", "qrc:/s7studio/help/docs/STUDIO_PAIR_ANALYSIS.md"},
    {"Live cursors, measurements and export", "qrc:/s7studio/help/docs/STUDIO_WINDOW_INSPECTION.md"},
    {"Live edge-trigger synchronization", "qrc:/s7studio/help/docs/STUDIO_TRIGGERING.md"},
    {"Bundled examples", "qrc:/s7studio/help/docs/STUDIO_EXAMPLES.md"},
    {"Saving and comparing results", "qrc:/s7studio/help/docs/SCOPE_RESULTS.md"},
    {"Observation viewers", "qrc:/s7studio/help/docs/STUDIO_OBSERVATION_VIEWERS.md"},
    {"Observers dock", "qrc:/s7studio/help/docs/STUDIO_OBSERVERS_DOCK.md"},
    {"Numerical validation", "qrc:/s7studio/help/docs/NUMERICAL_VALIDATION_MATRIX.md"},
    {"Preview limits", "qrc:/s7studio/help/docs/RELEASE_0.1.0.md"},
    licensing,
    thirdPartyNotices,
    {"Linear response laboratory", "qrc:/s7studio/help/examples/linear/README.md"},
    {"PI/PID control laboratory", "qrc:/s7studio/help/examples/control/README.md"},
    {"Buck converter", "qrc:/s7studio/help/examples/buck/README.md"},
    {"Relay thermostat", "qrc:/s7studio/help/examples/relay/README.md"},
    {"Nonlinear single pendulum", "qrc:/s7studio/help/examples/pendulum/README.md"},
    {"Small-angle double pendulum", "qrc:/s7studio/help/examples/double_pendulum/README.md"},
    {"Van der Pol oscillator", "qrc:/s7studio/help/examples/van_der_pol/README.md"},
    {"Lorenz system", "qrc:/s7studio/help/examples/lorenz/README.md"},
    {"Lotka–Volterra predator/prey", "qrc:/s7studio/help/examples/lotka_volterra/README.md"},
    {"Phase-locked loop", "qrc:/s7studio/help/examples/pll/README.md"},
    {"AM/FM and RMS measurement", "qrc:/s7studio/help/examples/signal_measurement/README.md"},
    {"PWM/PID/RLC regulator", "qrc:/s7studio/help/examples/pwm_pid_rlc.md"},
    {"2D oscillator phase portrait", "qrc:/s7studio/help/examples/2int_osc_phase_portrait.md"},
    {"3D helix trajectory", "qrc:/s7studio/help/examples/3int_helix_phase_trajectory.md"},
};

QString resourceText(const QString& url)
{
    QFile file(QStringLiteral(":") + QUrl(url).path());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}

void showWindow(QDialog* dialog)
{
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void addCloseButton(QDialog* dialog, QVBoxLayout* layout)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
}
}

void showHelp(QWidget* parent)
{
    if (auto* existing = parent->findChild<QDialog*>(QStringLiteral("dialog.help"))) {
        showWindow(existing);
        return;
    }
    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("dialog.help"));
    dialog->setWindowTitle(QStringLiteral("S7 Studio Help"));
    dialog->resize(960, 680);
    auto* layout = new QVBoxLayout(dialog);
    auto* search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("help.search"));
    search->setPlaceholderText(QStringLiteral("Search help topics and text…"));
    search->setAccessibleName(QStringLiteral("Search help"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto* noResults = new QLabel(QStringLiteral("No help topics match this search."), dialog);
    noResults->setObjectName(QStringLiteral("help.noResults"));
    noResults->hide();
    layout->addWidget(noResults);
    auto* splitter = new QSplitter(dialog);
    auto* contents = new QListWidget(splitter);
    contents->setObjectName(QStringLiteral("help.contents"));
    contents->setAccessibleName(QStringLiteral("Help topics"));
    auto* browser = new QTextBrowser(splitter);
    browser->setObjectName(QStringLiteral("help.browser"));
    browser->setAccessibleName(QStringLiteral("Help article"));
    browser->setOpenLinks(false);
    browser->setOpenExternalLinks(false);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 720});
    auto* navigation = new QHBoxLayout;
    auto* back = new QPushButton(QStringLiteral("Back"), dialog);
    auto* forward = new QPushButton(QStringLiteral("Forward"), dialog);
    auto* home = new QPushButton(QStringLiteral("Home"), dialog);
    navigation->addWidget(back);
    navigation->addWidget(forward);
    navigation->addWidget(home);
    navigation->addStretch();
    layout->addLayout(navigation);
    layout->addWidget(splitter);
    addCloseButton(dialog, layout);
    for (const auto& topic : topics) {
        auto* item = new QListWidgetItem(QString::fromUtf8(topic.title), contents);
        item->setData(Qt::UserRole, QString::fromUtf8(topic.path));
        item->setData(Qt::UserRole + 1, resourceText(QString::fromUtf8(topic.path)));
    }
    QObject::connect(contents, &QListWidget::currentItemChanged, browser,
                     [browser](QListWidgetItem* item) {
        if (item) browser->setSource(QUrl(item->data(Qt::UserRole).toString()), QTextDocument::MarkdownResource);
    });
    QObject::connect(search, &QLineEdit::textChanged, contents, [contents, noResults](const QString& text) {
        QListWidgetItem* first = nullptr;
        for (int i = 0; i < contents->count(); ++i) {
            auto* item = contents->item(i);
            const bool matches = (item->text() + '\n' + item->data(Qt::UserRole + 1).toString())
                .contains(text.trimmed(), Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches && !first) first = item;
        }
        contents->setCurrentItem(first);
        noResults->setVisible(first == nullptr);
    });
    QObject::connect(browser, &QTextBrowser::anchorClicked, browser, [browser](const QUrl& link) {
        const auto resolved = browser->source().resolved(link);
        if (resolved.scheme() == QStringLiteral("https")) {
            QDesktopServices::openUrl(resolved);
            return;
        }
        for (const auto& topic : topics) {
            if (resolved.adjusted(QUrl::RemoveFragment) == QUrl(QString::fromUtf8(topic.path))) {
                browser->setSource(resolved, QTextDocument::MarkdownResource);
                return;
            }
        }
    });
    QObject::connect(back, &QPushButton::clicked, browser, &QTextBrowser::backward);
    QObject::connect(forward, &QPushButton::clicked, browser, &QTextBrowser::forward);
    QObject::connect(browser, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
    QObject::connect(browser, &QTextBrowser::forwardAvailable, forward, &QPushButton::setEnabled);
    back->setEnabled(false);
    forward->setEnabled(false);
    QObject::connect(home, &QPushButton::clicked, dialog, [search, contents, browser] {
        search->clear();
        contents->setCurrentRow(0);
        browser->setSource(QUrl(QString::fromUtf8(topics[0].path)), QTextDocument::MarkdownResource);
    });
    contents->setCurrentRow(0);
    showWindow(dialog);
    search->setFocus();
}

void showAbout(QWidget* parent)
{
    if (auto* existing = parent->findChild<QDialog*>(QStringLiteral("dialog.about"))) {
        showWindow(existing);
        return;
    }
    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("dialog.about"));
    dialog->setWindowTitle(QStringLiteral("About S7 Studio"));
    dialog->resize(720, 560);
    auto* layout = new QVBoxLayout(dialog);
    auto* title = new QLabel(QStringLiteral("<h1>S7 Studio %1</h1><p>Build, run and observe causal graphs.</p>")
                             .arg(QString::fromUtf8(S7_BUILD_VERSION).toHtmlEscaped()), dialog);
    layout->addWidget(title);
    auto* tabs = new QTabWidget(dialog);
    auto* credits = new QTextBrowser(tabs);
    credits->setObjectName(QStringLiteral("about.credits"));
    credits->setHtml(QStringLiteral(
        "<h2>Author and maintainer</h2><p>Oleksiy Lukin</p>"
        "<h2>Contributors</h2><p>teren-ukr and S7 contributors.</p>"
        "<h2>Acknowledgements</h2><p>Developed with help from Claude, GitHub Copilot and Codex.</p>"
        "<p>Thanks to the Qt, QSchematic, GPDS and Tabler Icons projects and the other libraries used by S7.</p>"
        "<p><a href='https://github.com/alukin/s7'>Project and issue tracker</a></p>"));
    credits->setOpenExternalLinks(true);
    tabs->addTab(credits, QStringLiteral("Credits"));
    auto* build = new QTextBrowser(tabs);
    build->setObjectName(QStringLiteral("about.build"));
    build->setPlainText(QStringLiteral("S7 Studio %1\nRevision: %2%3\nCompiler: %4\nQt: %5\nSystem: %6\nArchitecture: %7")
        .arg(QString::fromUtf8(S7_BUILD_VERSION), QString::fromUtf8(S7_BUILD_REVISION),
             S7_BUILD_DIRTY ? QStringLiteral(" (modified source)") : QString{},
             QString::fromUtf8(S7_BUILD_COMPILER), QString::fromUtf8(qVersion()),
             QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
    tabs->addTab(build, QStringLiteral("Build"));
    for (const auto& topic : {licensing, thirdPartyNotices}) {
        auto* text = new QTextBrowser(tabs);
        text->setSource(QUrl(QString::fromUtf8(topic.path)), QTextDocument::MarkdownResource);
        text->setOpenLinks(false);
        tabs->addTab(text, QString::fromUtf8(topic.title));
    }
    auto* licence = new QTextBrowser(tabs);
    licence->setPlainText(resourceText(QStringLiteral("qrc:/s7studio/legal/LICENSES/GPL-3.0-or-later.txt")));
    tabs->addTab(licence, QStringLiteral("GNU GPL"));
    layout->addWidget(tabs);
    addCloseButton(dialog, layout);
    showWindow(dialog);
}
}
