#pragma once

#include <QGraphicsItem>
#include <QString>

enum class TopologyNodeType {
    Switch,
    Host
};

class TopologyNodeItem : public QGraphicsItem
{
public:
    TopologyNodeItem(const QString& id, const QString& label, TopologyNodeType type, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString id() const { return m_id; }
    void setOnline(bool online);

private:
    void paintEndpointIcon(QPainter* painter, const QRectF& iconRect, const QColor& fill, const QColor& border) const;
    void paintFcSwitchIcon(QPainter* painter, const QRectF& iconRect, const QColor& fill, const QColor& border) const;

    QString m_id;
    QString m_label;
    TopologyNodeType m_type;
    bool m_online = false;
};
