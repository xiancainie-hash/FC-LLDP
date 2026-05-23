#pragma once

#include <QGraphicsItem>
#include <QString>

class TopologyNodeItem;

enum class TopologyLinkStatus {
    Confirmed,
    Pending,
    Down
};

class TopologyLinkItem : public QGraphicsItem
{
public:
    TopologyLinkItem(TopologyNodeItem* a, TopologyNodeItem* b, const QString& label, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void setStatus(TopologyLinkStatus status);
    void setLabel(const QString& label);

private:
    TopologyNodeItem* m_a = nullptr;
    TopologyNodeItem* m_b = nullptr;
    QString m_label;
    TopologyLinkStatus m_status = TopologyLinkStatus::Pending;
};
