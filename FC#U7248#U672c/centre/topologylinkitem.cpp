#include "topologylinkitem.h"
#include "topologynodeitem.h"

#include <QFont>
#include <QPainter>
#include <QPen>

TopologyLinkItem::TopologyLinkItem(TopologyNodeItem* a, TopologyNodeItem* b, const QString& label, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_a(a), m_b(b), m_label(label)
{
    setZValue(1);
}

QRectF TopologyLinkItem::boundingRect() const
{
    if (!m_a || !m_b) return QRectF();
    const QPointF pa = mapFromScene(m_a->scenePos());
    const QPointF pb = mapFromScene(m_b->scenePos());
    return QRectF(pa, pb).normalized().adjusted(-110, -60, 110, 60);
}

void TopologyLinkItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    if (!m_a || !m_b) return;

    const QPointF pa = mapFromScene(m_a->scenePos());
    const QPointF pb = mapFromScene(m_b->scenePos());

    QPen pen;
    switch (m_status) {
    case TopologyLinkStatus::Confirmed:
        pen = QPen(Qt::black, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        break;
    case TopologyLinkStatus::Pending:
        pen = QPen(QColor(120, 120, 120), 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        break;
    case TopologyLinkStatus::Down:
        pen = QPen(QColor(235, 40, 65), 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        break;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(pen);
    painter->drawLine(pa, pb);

    QFont font = painter->font();
    font.setPointSize(9);
    painter->setFont(font);
    painter->setPen(Qt::black);
    const QPointF mid = (pa + pb) / 2.0;
    painter->drawText(QRectF(mid.x() - 105, mid.y() - 12, 210, 24), Qt::AlignCenter, m_label);
}

void TopologyLinkItem::setStatus(TopologyLinkStatus status)
{
    if (m_status == status) return;
    m_status = status;
    update();
}

void TopologyLinkItem::setLabel(const QString& label)
{
    if (m_label == label) return;
    m_label = label;
    update();
}
