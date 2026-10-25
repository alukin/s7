// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "observers_panel.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPixmap>
#include <QScopedValueRollback>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace s7studio::ui {
using namespace viewers;
namespace {
QString text(const boost::json::object& entry, const char* field) { return QString::fromStdString(std::string(entry.at(field).as_string())); }
const ObserverDescriptor* descriptorFor(const QVector<ObserverDescriptor>& descriptors, const QString& id, const QString& type) {
    for (const auto& descriptor : descriptors) if (descriptor.id == id && descriptor.type == type) return &descriptor;
    return nullptr;
}
QVector<int> selectedChannels(const boost::json::object& entry, int count) {
    QVector<int> result;
    if (const auto* value = entry.if_contains("plotted_channels")) { for (const auto& channel : value->as_array()) result.push_back(static_cast<int>(channel.as_int64())); }
    else for (int i = 0; i < std::min(8, count); ++i) result.push_back(i);
    return result;
}
QLabel* label(QWidget* parent) { auto* value = new QLabel(parent); value->setTextFormat(Qt::PlainText); value->setWordWrap(true); value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); return value; }
}
struct ObserversPanel::Card {
    QString id, type, key;
    boost::json::object selection;
    QPointer<QWidget> widget;
    QWidget *body = nullptr, *viewport = nullptr;
    QVBoxLayout* plotLayout = nullptr;
    QLabel *status = nullptr, *placeholder = nullptr, *binding = nullptr;
    QToolButton *toggle = nullptr, *freeze = nullptr, *channels = nullptr;
    QPlainTextEdit* details = nullptr;
    QWidget* legend = nullptr;
    QStringList legendNames;
    QPointer<ObserverPresentation> presentation;
    PreviewWorker worker;
    std::optional<PreviewData> last;
    std::uint64_t revision = 0;
    void deactivate() {
        worker.cancel(); const auto previous = presentation; presentation = nullptr;
        // Quick scene construction can pump events from a child's showEvent.
        // Never synchronously destroy that surface from a nested panel tick.
        if (previous) { previous->hide(); previous->deleteLater(); }
        if (!widget) return;
        if (!freeze->isChecked()) last.reset();
        placeholder->show();
    }
};
ObserversPanel::ObserversPanel(ViewerManager* manager, QWidget* parent) : QWidget(parent), manager_(manager)
{
    setObjectName("observers.panel"); setMinimumWidth(300);
    auto* root = new QVBoxLayout(this); root->setContentsMargins(4, 4, 4, 4);
    auto* choose = new QPushButton("Choose Observers…", this); choose->setObjectName("observers.choose"); root->addWidget(choose);
    root_ = label(this); root_->setObjectName("observers.root"); root->addWidget(root_);
    state_ = label(this); state_->setObjectName("observers.state"); root->addWidget(state_);
    empty_ = label(this); empty_->setObjectName("observers.empty"); root->addWidget(empty_);
    scroll_ = new QScrollArea(this); scroll_->setObjectName("observers.scroll"); scroll_->setWidgetResizable(true); scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll_); cardsLayout_ = new QVBoxLayout(content); cardsLayout_->setContentsMargins(0, 0, 0, 0); cardsLayout_->addStretch();
    scroll_->setWidget(content); root->addWidget(scroll_, 1);
    auto* recordedButton = new QPushButton("Recorded Signals…", this); recordedButton->setObjectName("observers.recordedSignals"); root->addWidget(recordedButton);
    connect(recordedButton, &QPushButton::clicked, this, &ObserversPanel::recordedSignalsRequested);
    connect(choose, &QPushButton::clicked, this, &ObserversPanel::chooseObservers);
    connect(manager_, &ViewerManager::observerSettingsChanged, this, [this](const QString& id) {
        for (auto& card : cards_) if (card->id == id && card->type == "LiveOscilloscope") {
            card->revision = ++revision_; card->worker.cancel(); card->last.reset(); card->details->clear();
            if (card->presentation) card->presentation->present({});
            card->status->setText("Trigger settings changed — waiting for a new completed capture");
        }
    });
    timer_ = new QTimer(this); timer_->setInterval(100); connect(timer_, &QTimer::timeout, this, &ObserversPanel::refresh); timer_->start();
    setRunState(RunState::Idle, 0, 0); synchronize();
}
ObserversPanel::~ObserversPanel() { timer_->stop(); for (auto& card : cards_) card->worker.cancel(); cards_.clear(); }
void ObserversPanel::setDocument(QString root, QVector<ObserverDescriptor> observers, boost::json::array selection) {
    root_->setText("Root: " + root); if (document_ == observers && selection_ == selection) return;
    document_ = std::move(observers); selection_ = std::move(selection); synchronize();
}
void ObserversPanel::bindRun(std::uint64_t id, QVector<ObserverDescriptor> observers) {
    runId_ = id; run_ = std::move(observers); dismissed_.clear();
    for (auto& card : cards_) { card->revision = ++revision_; card->deactivate(); card->last.reset(); card->details->clear(); const QSignalBlocker block(card->freeze); card->freeze->setChecked(false); }
    synchronize();
}
void ObserversPanel::clearRun() {
    runId_ = 0; run_.clear(); dismissed_.clear();
    for (auto& card : cards_) { card->revision = ++revision_; card->deactivate(); card->last.reset(); card->details->clear(); const QSignalBlocker block(card->freeze); card->freeze->setChecked(false); }
    synchronize(); setRunState(RunState::Idle, 0, 0);
}
void ObserversPanel::setRunState(RunState state, long step, double dt, const QString& error) {
    state_->setText(QStringLiteral("Run %1 • %2 • step %3 • model %4 s%5").arg(runId_).arg(runStateName(state)).arg(step).arg(static_cast<double>(step)*dt, 0, 'g', 12)
        .arg(error.isEmpty() ? QString() : " • " + error));
}
int ObserversPanel::activePresentations() const { int active = 0; for (const auto& card : cards_) if (card->presentation) ++active; return active; }
void ObserversPanel::updateSelection(const QString& identity, const std::function<void(boost::json::object&)>& edit) {
    for (auto& value : selection_) if (key(text(value.as_object(), "element_id"), text(value.as_object(), "type")) == identity) { edit(value.as_object()); break; }
    emit selectionChanged(selection_); synchronize();
}
void ObserversPanel::synchronize()
{
    QSet<QString> wanted; QStringList order;
    for (const auto& value : selection_) {
        const auto& entry = value.as_object(); const auto id = text(entry, "element_id"), type = text(entry, "type"), identity = key(id, type);
        if (!descriptorFor(document_, id, type)) continue;
        wanted.insert(identity); order.push_back(identity);
        if (!cards_.contains(identity)) {
            auto card = std::make_shared<Card>(); card->id = id; card->type = type; card->key = identity; card->revision = ++revision_;
            card->widget = new QWidget(scroll_->widget()); card->widget->setObjectName("observer.card."+id); auto* layout = new QVBoxLayout(card->widget); layout->setContentsMargins(2, 4, 2, 4);
            auto* title = label(card->widget); const auto* registered = ViewerRegistry::instance().find(type);
            title->setText(id+"\n"+(registered ? registered->displayName : type)); title->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(title);
            auto* header = new QHBoxLayout; card->toggle = new QToolButton(card->widget); card->toggle->setObjectName("observer.collapse."+id);
            card->toggle->setCheckable(true); card->toggle->setText("Preview"); card->toggle->setAccessibleName("Expand or collapse "+id); card->toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            card->toggle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); card->toggle->setToolTip(id+" · "+type); header->addWidget(card->toggle, 1);
            auto* open = new QPushButton("Open", card->widget); open->setObjectName("observer.open."+id); header->addWidget(open);
            auto* remove = new QPushButton("Remove", card->widget); remove->setObjectName("observer.remove."+id); header->addWidget(remove); layout->addLayout(header);
            card->binding = label(card->widget); layout->addWidget(card->binding);
            card->body = new QWidget(card->widget); auto* body = new QVBoxLayout(card->body); body->setContentsMargins(0, 0, 0, 0); layout->addWidget(card->body);
            auto* tools = new QHBoxLayout; card->freeze = new QToolButton(card->body); card->freeze->setObjectName("observer.freeze."+id); card->freeze->setText("Freeze preview"); card->freeze->setCheckable(true); tools->addWidget(card->freeze);
            card->channels = new QToolButton(card->body); card->channels->setText("Plotted channels…"); card->channels->setObjectName("observer.channels."+id); tools->addWidget(card->channels); body->addLayout(tools);
            card->viewport = new QWidget(card->body); card->viewport->setFixedHeight(180); card->plotLayout = new QVBoxLayout(card->viewport); card->plotLayout->setContentsMargins(0, 0, 0, 0);
            card->placeholder = label(card->viewport); card->placeholder->setText("Recent bounded preview; waiting for an active viewport."); card->plotLayout->addWidget(card->placeholder); body->addWidget(card->viewport);
            card->legend = new QWidget(card->body); card->legend->setObjectName("observer.legend."+id); auto* legendLayout = new QGridLayout(card->legend); legendLayout->setContentsMargins(0,0,0,0); legendLayout->setSpacing(2); card->legend->hide(); body->addWidget(card->legend);
            card->status = label(card->body); card->status->setObjectName("observer.status."+id); body->addWidget(card->status);
            auto* details = new QToolButton(card->body); details->setText("Channels / latest sampled values"); details->setCheckable(true); body->addWidget(details);
            card->details = new QPlainTextEdit(card->body); card->details->setObjectName("observer.details."+id); card->details->setReadOnly(true); card->details->setMaximumHeight(150); card->details->hide(); body->addWidget(card->details);
            card->details->setUndoRedoEnabled(false); card->details->setMaximumBlockCount(64);
            connect(details, &QToolButton::toggled, card->details, [card = std::weak_ptr<Card>(card)](bool shown) {
                if (auto current = card.lock()) { current->details->setVisible(shown); if (shown && current->last) current->details->setPlainText(current->last->details.join('\n')); }
            });
            cards_.insert(identity, card);
            connect(open, &QPushButton::clicked, this, [this, id, type] { manager_->openObserver(type, id); });
            connect(remove, &QPushButton::clicked, this, [this, identity] {
                dismissed_.insert(identity);
                selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [&](const auto& v) { return key(text(v.as_object(), "element_id"), text(v.as_object(), "type")) == identity; }), selection_.end());
                emit selectionChanged(selection_); synchronize();
            });
            connect(card->toggle, &QToolButton::toggled, this, [this, identity](bool expanded) {
                if (auto card = cards_.value(identity)) { card->body->setVisible(expanded); card->toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow); }
                updateSelection(identity, [expanded](auto& value) { value["collapsed"] = !expanded; });
            });
            connect(card->freeze, &QToolButton::toggled, this, [this, identity] { if (auto card = cards_.value(identity)) { card->worker.cancel(); card->revision = ++revision_; } });
            connect(card->channels, &QToolButton::clicked, this, [this, identity] {
                auto card = cards_.value(identity); if (!card) return;
                const auto* descriptor = runId_ ? descriptorFor(run_, card->id, card->type) : descriptorFor(document_, card->id, card->type); if (!descriptor) return;
                QDialog dialog(this); dialog.setWindowTitle("Plotted channels — "+card->id); auto* layout = new QVBoxLayout(&dialog); auto* list = new QListWidget(&dialog);
                const auto selected = selectedChannels(card->selection, static_cast<int>(descriptor->inputs.size()));
                for (int i = 0; i < descriptor->inputs.size(); ++i) { auto* item = new QListWidgetItem(descriptor->inputs[i].name, list); item->setCheckState(selected.contains(i) ? Qt::Checked : Qt::Unchecked); }
                auto* notice = new QLabel(&dialog); notice->setWordWrap(true); layout->addWidget(notice); layout->addWidget(list);
                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
                const auto validate = [list, notice, buttons] {
                    int count = 0; for (int i=0; i<list->count(); ++i) count += list->item(i)->checkState()==Qt::Checked;
                    buttons->button(QDialogButtonBox::Ok)->setEnabled(count<=8);
                    notice->setText(QStringLiteral("%1 selected (maximum 8). %2 Capture remains unchanged.").arg(count).arg(count>8 ? "Uncheck channels to continue." : ""));
                };
                connect(list, &QListWidget::itemChanged, &dialog, validate); validate();
                connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                if (dialog.exec() == QDialog::Accepted) updateSelection(identity, [&](auto& value) { boost::json::array channels; for (int i=0; i<list->count(); ++i) if (list->item(i)->checkState()==Qt::Checked) channels.push_back(i); value["plotted_channels"] = std::move(channels); });
            });
        }
        auto card = cards_.value(identity);
        if (card->selection != entry) { card->revision = ++revision_; card->worker.cancel(); }
        card->selection = entry; const bool expanded = !entry.at("collapsed").as_bool();
        const QSignalBlocker block(card->toggle); card->toggle->setChecked(expanded); card->toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow); card->body->setVisible(expanded);
        card->channels->setVisible(type == "Scope" || type == "LiveOscilloscope");
    }
    // Deleted/type-changed active bindings remain inspectable until the run retires.
    for (auto it = cards_.cbegin(); it != cards_.cend(); ++it)
        if (!wanted.contains(it.key()) && !dismissed_.contains(it.key()) && runId_ && descriptorFor(run_, it.value()->id, it.value()->type) && !descriptorFor(document_, it.value()->id, it.value()->type)) { wanted.insert(it.key()); order.push_back(it.key()); }
    for (const auto& identity : cards_.keys()) if (!wanted.contains(identity)) { auto card = cards_.take(identity); card->worker.cancel(); delete card->widget; }
    for (int i = 0; i < order.size(); ++i) { auto card = cards_.value(order[i]); cardsLayout_->removeWidget(card->widget); cardsLayout_->insertWidget(i, card->widget); }
    empty_->setText(document_.isEmpty() ? "No root observers. Place an observer from the library and connect its inputs." : "No previews selected. Use Choose Observers…"); empty_->setVisible(cards_.isEmpty());
}
void ObserversPanel::refresh()
{
    if (refreshing_) return;
    const QScopedValueRollback guard(refreshing_, true);
    const bool visible = isVisible() && !window()->isMinimized(); int active = 0; bool created = false;
    // Layout order determines allocation, including when selection was loaded from disk.
    for (int i = 0; i < cardsLayout_->count()-1; ++i) {
        auto* widget = cardsLayout_->itemAt(i)->widget(); std::shared_ptr<Card> card;
        for (const auto& value : cards_) if (value->widget == widget) { card = value; break; }
        if (!card) continue;
        const auto* configured = descriptorFor(document_, card->id, card->type);
        const auto* runBinding = runId_ ? descriptorFor(run_, card->id, card->type) : nullptr;
        const std::optional<ObserverDescriptor> binding = runBinding ? std::optional{*runBinding} : std::nullopt;
        card->binding->setText(binding && (!configured || *binding != *configured) ? "Active-run binding retained • document changes apply next run" : runId_ && !binding ? "Next run — no matching active binding" : "");
        const QRect rect(card->viewport->mapTo(scroll_->viewport(), QPoint()), card->viewport->size());
        const bool inViewport = rect.intersects(scroll_->viewport()->rect());
        const bool eligible = visible && card->toggle->isChecked() && inViewport;
        const bool allocated = eligible && active < 4;
        if (!allocated) { if (card->presentation || card->worker.busy()) card->deactivate(); card->placeholder->setText(eligible ? "Preview resource limit — four visible plots at a time" : "Preview suspended outside the visible viewport"); continue; }
        if (!binding || binding->inputs.isEmpty()) { card->status->setText(binding ? "Unconnected observer — connect inputs for the next run" : "Waiting for this observer's next run"); continue; }
        ++active;
        if (!card->presentation) {
            if (created) { card->placeholder->setText("Preparing preview…"); continue; }
            const auto revision = card->revision;
            const auto* type = ViewerRegistry::instance().find(card->type);
            if (!type || !type->createPresentation) { card->status->setText("Preview unavailable for this viewer. Use Open for its full window."); continue; }
            card->presentation = type->createPresentation(nullptr); card->plotLayout->addWidget(card->presentation);
            created = true;
            if (!card->widget || card->revision != revision || !card->presentation) continue;
            card->placeholder->hide();
            connect(card->presentation.data(), &ObserverPresentation::navigated, card->freeze, [button = card->freeze] { button->setChecked(true); });
            if (card->last) card->presentation->present(*card->last);
        }
        if (auto data = card->worker.takeResult(); data && data->revision == card->revision && (!data->run || data->run == runId_)) {
            card->last = std::move(data); card->presentation->present(*card->last); card->status->setText(card->last->status);
            if (card->type == "Scope" || card->type == "LiveOscilloscope") {
                QStringList names; for (const auto& trace : card->last->traces) names.push_back(trace.name);
                if (names != card->legendNames) {
                    card->legendNames = names; auto* grid = static_cast<QGridLayout*>(card->legend->layout());
                    while (auto* item = grid->takeAt(0)) { delete item->widget(); delete item; }
                    for (int channel=0;channel<card->last->traces.size();++channel) {
                        const auto& trace = card->last->traces[channel]; auto* cell = new QWidget(card->legend); auto* row = new QHBoxLayout(cell); row->setContentsMargins(0,0,0,0); row->setSpacing(3);
                        auto* swatch = new QLabel(cell); QPixmap pixels(10,10); pixels.fill(ObserverPresentation::channelColor(card->type,trace.channel)); swatch->setPixmap(pixels); row->addWidget(swatch);
                        auto* name = label(cell); name->setText(trace.name.section('/',-1)); name->setToolTip(trace.name); row->addWidget(name,1); grid->addWidget(cell,channel/2,channel%2);
                    }
                    card->legend->setVisible(!names.empty());
                }
            }
            if (card->details->isVisible()) card->details->setPlainText(card->last->details.join('\n'));
        }
        if (card->freeze->isChecked()) continue;
        PreviewRequest request{*binding, selectedChannels(card->selection, static_cast<int>(binding->inputs.size())), manager_->source(), manager_->liveSource(card->id), manager_->observerTrigger(card->id), card->revision};
        card->worker.submit(std::move(request)); ++submitted_;
    }
}
void ObserversPanel::hideEvent(QHideEvent* event) { for (auto& card : cards_) card->deactivate(); QWidget::hideEvent(event); }
void ObserversPanel::chooseObservers()
{
    QDialog dialog(this); dialog.setObjectName("observers.chooser"); dialog.setWindowTitle("Choose root observers"); dialog.resize(520, 420);
    auto* layout = new QVBoxLayout(&dialog); auto* search = new QLineEdit(&dialog); search->setObjectName("observers.search"); search->setPlaceholderText("Filter observers"); search->setClearButtonEnabled(true); layout->addWidget(search);
    auto* list = new QListWidget(&dialog); list->setObjectName("observers.choices"); layout->addWidget(list);
    for (const auto& descriptor : document_) {
        auto* item = new QListWidgetItem(QStringLiteral("%1 · %2 · %3 inputs connected").arg(descriptor.id, descriptor.type).arg(descriptor.inputs.size()), list);
        const auto identity = key(descriptor.id, descriptor.type); item->setData(Qt::UserRole, identity);
        bool checked = false; for (const auto& value : selection_) checked |= key(text(value.as_object(), "element_id"), text(value.as_object(), "type")) == identity;
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    connect(search, &QLineEdit::textChanged, list, [list](const QString& filter) { for (int i=0;i<list->count();++i) list->item(i)->setHidden(!list->item(i)->text().contains(filter, Qt::CaseInsensitive)); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dialog); buttons->button(QDialogButtonBox::Ok)->setText("Apply"); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    QSet<QString> chosen; for (int i=0;i<list->count();++i) if (list->item(i)->checkState()==Qt::Checked) chosen.insert(list->item(i)->data(Qt::UserRole).toString());
    boost::json::array result;
    for (const auto& value : selection_) if (chosen.remove(key(text(value.as_object(), "element_id"), text(value.as_object(), "type")))) result.push_back(value);
    for (const auto& descriptor : document_) if (chosen.contains(key(descriptor.id, descriptor.type))) result.push_back(boost::json::object{{"element_id", descriptor.id.toStdString()}, {"type", descriptor.type.toStdString()}, {"collapsed", false}});
    selection_ = std::move(result); emit selectionChanged(selection_); synchronize();
}
}
