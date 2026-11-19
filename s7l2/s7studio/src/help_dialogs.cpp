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
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <memory>

namespace s7studio {
namespace {
struct Topic { const char* title; const char* path; };
constexpr Topic licensing{"Licensing", "qrc:/s7studio/legal/LICENSING.md"};
constexpr Topic thirdPartyNotices{"Third-party notices", "qrc:/s7studio/legal/THIRD_PARTY_NOTICES.md"};
constexpr Topic topics[] = {
    {"Getting started", "qrc:/s7studio/help/docs/STUDIO_USER_GUIDE.md"},
    {"Basic block symbols", "qrc:/s7studio/help/docs/DIAGRAM_SYMBOLS.md"},
    {"Integrators, delays, and s/z mappings", "qrc:/s7studio/help/docs/INTEGRATORS.md"},
    {"Integrator references: Siebert and MIT", "qrc:/s7studio/help/docs/INTEGRATOR_REFERENCES.md"},
    {"Trapezoid implementation", "qrc:/s7studio/help/docs/TRAPEZOID_INTEGRATION.md"},
    {"Coupled integration implementation", "qrc:/s7studio/help/docs/COUPLED_INTEGRATION.md"},
    {"Live oscilloscope prototype", "qrc:/s7studio/help/docs/LIVE_OSCILLOSCOPE_PROTOTYPE.md"},
    {"Live FFT spectrum", "qrc:/s7studio/help/docs/STUDIO_SPECTRUM.md"},
    {"Live constellation and correlation", "qrc:/s7studio/help/docs/STUDIO_PAIR_ANALYSIS.md"},
    {"Live cursors, measurements and export", "qrc:/s7studio/help/docs/STUDIO_WINDOW_INSPECTION.md"},
    {"Live edge-trigger synchronization", "qrc:/s7studio/help/docs/STUDIO_TRIGGERING.md"},
    {"Bundled examples", "qrc:/s7studio/help/docs/STUDIO_EXAMPLES.md"},
    {"Education: start here", "qrc:/s7studio/help/examples/education/README.md"},
    {"Education: FN01 one sample of memory", "qrc:/s7studio/help/education/labs/FN01-unit-delay/README.md"},
    {"Education: FN02 natural and forced response", "qrc:/s7studio/help/education/labs/FN02-natural-forced/README.md"},
    {"Education: FN03 initial conditions", "qrc:/s7studio/help/education/labs/FN03-initial-conditions/README.md"},
    {"Education: FN04 impulse transfer", "qrc:/s7studio/help/education/labs/FN04-impulse-transfer/README.md"},
    {"Education: FN05 integration rules", "qrc:/s7studio/help/education/labs/FN05-integration-rules/README.md"},
    {"Education: FN06 pole maps", "qrc:/s7studio/help/education/labs/FN06-pole-maps/README.md"},
    {"Education: CT01 first order", "qrc:/s7studio/help/education/labs/CT01-first-order/README.md"},
    {"Education: CT02 damped modes", "qrc:/s7studio/help/education/labs/CT02-damped-modes/README.md"},
    {"Education: CT03 sample time", "qrc:/s7studio/help/education/labs/CT03-sample-time/README.md"},
    {"Education: CT04 controller comparison", "qrc:/s7studio/help/education/labs/CT04-controller-comparison/README.md"},
    {"Education: CT05 actuator windup", "qrc:/s7studio/help/education/labs/CT05-actuator-windup/README.md"},
    {"Education: CT06 closed-loop poles", "qrc:/s7studio/help/education/labs/CT06-closed-loop-poles/README.md"},
    {"Education: CT07 stability margins", "qrc:/s7studio/help/education/labs/CT07-stability-margins/README.md"},
    {"Education: SP01 Fourier synthesis", "qrc:/s7studio/help/education/labs/SP01-fourier-synthesis/README.md"},
    {"Education: SP03 sampling and aliasing", "qrc:/s7studio/help/education/labs/SP03-aliasing/README.md"},
    {"Education: SP05 shifted impulse responses", "qrc:/s7studio/help/education/labs/SP05-convolution/README.md"},
    {"Education: SP10 circular versus linear convolution", "qrc:/s7studio/help/education/labs/SP10-circular-convolution/README.md"},
    {"Education: SP11 short-time Fourier transform", "qrc:/s7studio/help/education/labs/SP11-short-time-fourier/README.md"},
    {"FIR and FFT convolution implementation", "qrc:/s7studio/help/docs/CONVOLUTION.md"},
    {"Education: SP07 filter transfer functions", "qrc:/s7studio/help/education/labs/SP07-filter-transfer/README.md"},
    {"Education: SP08 DFT leakage", "qrc:/s7studio/help/education/labs/SP08-dft-leakage/README.md"},
    {"Education: SP09 window normalization", "qrc:/s7studio/help/education/labs/SP09-window-normalization/README.md"},
    {"Education: SP06 three-tap FIR", "qrc:/s7studio/help/education/labs/SP06-three-tap-fir/README.md"},
    {"Saving and comparing results", "qrc:/s7studio/help/docs/SCOPE_RESULTS.md"},
    {"Observation viewers", "qrc:/s7studio/help/docs/STUDIO_OBSERVATION_VIEWERS.md"},
    {"Observers dock", "qrc:/s7studio/help/docs/STUDIO_OBSERVERS_DOCK.md"},
    {"Numerical validation", "qrc:/s7studio/help/docs/NUMERICAL_VALIDATION_MATRIX.md"},
    {"Preview limits", "qrc:/s7studio/help/docs/RELEASE_0.1.2_NOTES.md"},
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

bool isWebLink(const QUrl& url)
{
    return url.isValid() && !url.host().isEmpty() &&
        (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
}

bool isHelpArticle(const QUrl& url)
{
    return url.scheme() == QStringLiteral("qrc") && url.path().endsWith(QStringLiteral(".md")) &&
        (url.path().startsWith(QStringLiteral("/s7studio/help/")) ||
         url.path().startsWith(QStringLiteral("/s7studio/legal/"))) &&
        QFile::exists(QStringLiteral(":") + url.path());
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
    auto* linkStatus = new QLabel(dialog);
    linkStatus->setObjectName(QStringLiteral("help.linkStatus"));
    linkStatus->setWordWrap(true);
    linkStatus->hide();
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
    layout->addWidget(linkStatus);
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
    const auto openExternal = [linkStatus](const QUrl& url) {
        const bool opened = QDesktopServices::openUrl(url);
        linkStatus->setText(opened ? QString{} : QStringLiteral("Could not open the external browser for %1").arg(url.toDisplayString()));
        linkStatus->setVisible(!opened);
    };
    QObject::connect(browser, &QTextBrowser::anchorClicked, browser, [browser, linkStatus, openExternal](const QUrl& link) {
        const auto resolved = browser->source().resolved(link).adjusted(QUrl::NormalizePathSegments);
        if (isWebLink(resolved)) {
            openExternal(resolved);
            return;
        }
        if (isHelpArticle(resolved)) {
            linkStatus->hide();
            browser->setSource(resolved, QTextDocument::MarkdownResource);
        } else {
            linkStatus->setText(QStringLiteral("This link is not available in bundled help: %1").arg(link.toDisplayString()));
            linkStatus->show();
        }
    });
    browser->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(browser, &QWidget::customContextMenuRequested, browser, [browser, openExternal](const QPoint& position) {
        const auto anchor = browser->anchorAt(position);
        const auto url = browser->source().resolved(QUrl(anchor));
        std::unique_ptr<QMenu> menu(browser->createStandardContextMenu(position));
        if (!anchor.isEmpty() && isWebLink(url)) {
            menu->addSeparator();
            auto* action = menu->addAction(QStringLiteral("Open Link in External Browser"));
            action->setObjectName(QStringLiteral("help.openExternalLink"));
            QObject::connect(action, &QAction::triggered, browser, [url, openExternal] { openExternal(url); });
        }
        menu->exec(browser->viewport()->mapToGlobal(position));
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

void openHelpLink(QWidget* parent, const QUrl& link)
{
    const auto normalized = link.adjusted(QUrl::NormalizePathSegments);
    if (!isHelpArticle(normalized) && !isWebLink(normalized)) return;
    if (isWebLink(normalized)) {
        if (QDesktopServices::openUrl(normalized)) return;
        showHelp(parent);
        auto* status = parent->findChild<QLabel*>(QStringLiteral("help.linkStatus"));
        status->setText(QStringLiteral("Could not open the external browser for %1").arg(normalized.toDisplayString()));
        status->show();
        return;
    }
    showHelp(parent);
    auto* dialog = parent->findChild<QDialog*>(QStringLiteral("dialog.help"));
    auto* browser = dialog->findChild<QTextBrowser*>(QStringLiteral("help.browser"));
    // Share normal help navigation, including browser-launch failure feedback.
    Q_EMIT browser->anchorClicked(normalized);
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
        "<h2>Acknowledgements</h2>"
        "<p>In memory of Serhiy Kondratiuk, my friend and collaborator on the first S7 implementation "
        "30 years ago, whose ideas continue to guide its development.</p>"
        "<p>Thanks to ChatGPT and Codex (OpenAI), Claude Code (Anthropic), and GitHub Copilot "
        "for assistance with design, implementation, testing and documentation.</p>"
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
