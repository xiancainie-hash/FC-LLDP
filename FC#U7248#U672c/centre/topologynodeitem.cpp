#include "topologynodeitem.h"

#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

TopologyNodeItem::TopologyNodeItem(const QString& id, const QString& label, TopologyNodeType type, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_id(id), m_label(label), m_type(type)
{
    setZValue(10);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
}

QRectF TopologyNodeItem::boundingRect() const
{
    return QRectF(-42, -32, 84, 78);
}

void TopologyNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing, true);
    const QColor fill = m_online ? QColor(35, 145, 225) : QColor(165, 165, 165);
    const QColor border = QColor(18, 18, 18);
    const QRectF iconRect(-31, -26, 62, 42);

    if (m_type == TopologyNodeType::Host) paintEndpointIcon(painter, iconRect, fill, border);
    else paintFcSwitchIcon(painter, iconRect, fill, border);

    QFont font = painter->font();
    font.setPointSize(10);
    painter->setFont(font);
    painter->setPen(Qt::black);
    painter->drawText(QRectF(-42, 20, 84, 24), Qt::AlignCenter, m_label);
}

void TopologyNodeItem::paintEndpointIcon(QPainter* painter, const QRectF& iconRect, const QColor& fill, const QColor& border) const
{
    painter->setPen(QPen(border, 2));
    painter->setBrush(fill);
    painter->drawRoundedRect(iconRect, 5, 5);
    const QRectF screen(iconRect.left() + 10, iconRect.top() + 8, iconRect.width() - 20, iconRect.height() - 19);
    painter->setBrush(QColor(238, 238, 238));
    painter->setPen(QPen(border, 1.8));
    painter->drawRect(screen);
    const QPointF standTop(screen.center().x(), screen.bottom());
    painter->drawLine(standTop, QPointF(standTop.x(), standTop.y() + 6));
    painter->drawLine(QPointF(standTop.x() - 12, standTop.y() + 6), QPointF(standTop.x() + 12, standTop.y() + 6));
}

void TopologyNodeItem::paintFcSwitchIcon(QPainter* painter, const QRectF& iconRect, const QColor& fill, const QColor& border) const
{
    painter->setPen(QPen(border, 2));
    painter->setBrush(fill);
    painter->drawRoundedRect(iconRect, 5, 5);

    painter->setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const QPointF c = iconRect.center();
    const qreal r = 17;
    QPolygonF diamond;
    diamond << QPointF(c.x(), c.y() - r) << QPointF(c.x() + r, c.y())
            << QPointF(c.x(), c.y() + r) << QPointF(c.x() - r, c.y());
    painter->drawPolygon(diamond);
    painter->drawLine(QPointF(c.x() - r + 5, c.y()), QPointF(c.x() + r - 5, c.y()));
    painter->drawLine(QPointF(c.x(), c.y() - r + 5), QPointF(c.x(), c.y() + r - 5));
}

void TopologyNodeItem::setOnline(bool online)
{
    if (m_online == online) return;
    m_online = online;
    update();
}
