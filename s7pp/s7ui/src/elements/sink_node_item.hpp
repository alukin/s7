#pragma once

#include "basic_node_item.hpp"

#include <s7probe.hpp>

#include <memory>
#include <QVector>

class QGraphicsProxyWidget;
class QGraphicsSceneMouseEvent;
class QTimer;

namespace s7ui::elements {

class VisualSinkDetailWindow;
class VisualSinkPreviewWidget;

class SinkNodeItem : public BasicNodeItem
{
    Q_OBJECT

public:
    explicit SinkNodeItem(const QString& typeName, int inputCount = 2, QGraphicsItem* parent = nullptr);
    ~SinkNodeItem() override;

    [[nodiscard]] std::shared_ptr<QSchematic::Items::Item> deepCopy() const override;

    void setInputCount(int count);
    void setPreviewLevels(const QVector<double>& levels);

    /// Attach a live Probe.  The SinkNodeItem holds a weak_ptr; when the probe
    /// is destroyed (session ends) the internal QTimer stops gracefully.
    void setProbe(std::shared_ptr<s7::Probe> probe);
    void setComplexProbe(std::shared_ptr<s7::ComplexProbe> probe);
    void clearProbe();

    [[nodiscard]] std::shared_ptr<s7::Probe> probe() const { return probe_.lock(); }

protected:
    void sizeChangedEvent(QSizeF oldSize, QSizeF newSize) override;
    void openPropertiesDialog() override;

private slots:
    void pollProbeData();

private:
    void updatePreviewGeometry();
    void startPolling();
    void stopPolling();

    QString typeName_;
    bool complexMode_ = false;
    int inputCount_ = 2;
    QGraphicsProxyWidget* proxy_   = nullptr;
    VisualSinkPreviewWidget* preview_ = nullptr;
    std::unique_ptr<VisualSinkDetailWindow> detailWindow_;

    std::weak_ptr<s7::Probe> probe_;
    std::weak_ptr<s7::ComplexProbe> complexProbe_;
    QTimer* pollTimer_ = nullptr;  // 30 Hz polling of the probe ring buffer
};

} // namespace s7ui::elements

