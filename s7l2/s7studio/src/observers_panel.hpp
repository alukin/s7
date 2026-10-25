// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "viewers/observer_presentation.hpp"
#include "is_schema_runner.hpp"
#include <QHash>
#include <QSet>
#include <boost/json.hpp>
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QTimer;
namespace s7studio::viewers { class ViewerManager; }
namespace s7studio::ui {
class ObserversPanel : public QWidget {
    Q_OBJECT
public:
    explicit ObserversPanel(viewers::ViewerManager* manager, QWidget* parent = nullptr);
    ~ObserversPanel() override;
    void setDocument(QString root, QVector<viewers::ObserverDescriptor> observers, boost::json::array selection);
    void bindRun(std::uint64_t id, QVector<viewers::ObserverDescriptor> observers);
    void clearRun();
    void setRunState(RunState state, long step, double dt, const QString& error = {});
    void chooseObservers();
    int activePresentations() const;
    std::uint64_t submittedRequests() const { return submitted_; }
    boost::json::array selection() const { return selection_; }
signals:
    void selectionChanged(const boost::json::array& selection);
    void recordedSignalsRequested();
protected:
    void hideEvent(QHideEvent*) override;
private:
    struct Card;
    void synchronize();
    void refresh();
    void updateSelection(const QString& key, const std::function<void(boost::json::object&)>& edit);
    static QString key(const QString& id, const QString& type) { return type+"\n"+id; }
    viewers::ViewerManager* manager_;
    QLabel *root_, *state_, *empty_;
    QScrollArea* scroll_;
    QVBoxLayout* cardsLayout_;
    QTimer* timer_;
    QVector<viewers::ObserverDescriptor> document_, run_;
    QHash<QString, std::shared_ptr<Card>> cards_;
    QSet<QString> dismissed_;
    boost::json::array selection_;
    std::uint64_t runId_ = 0, revision_ = 0, submitted_ = 0;
    bool refreshing_ = false;
};
}
