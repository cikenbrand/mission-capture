/******************************************************************************
    Copyright (C) 2026 by Cyberian Resources

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "MCLayersDelegate.hpp"
#include "MCLayersModel.hpp"

#include <widgets/OBSBasic.hpp>

#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>

#include "moc_MCLayersDelegate.cpp"

namespace {

bool isElement(const QModelIndex &index)
{
	return index.data(MCLayersModel::KindRole).toInt() == static_cast<int>(MCLayersModel::Kind::Element);
}

QIcon iconFor(const QModelIndex &index)
{
	OBSBasic *main = OBSBasic::Get();
	if (!main) {
		return {};
	}

	if (!isElement(index)) {
		return main->GetSceneIcon();
	}

	const QString sourceId = index.data(MCLayersModel::SourceIdRole).toString();
	return main->GetSourceIcon(QT_TO_UTF8(sourceId));
}

/* Simple glyphs rather than themed pixmaps: the theme's icons are sized and
 * coloured for upstream's checkbox rows, and Phase 1 has no artwork of its own
 * yet. Legible at a glance is the requirement, not decoration. */
void paintEye(QPainter *p, const QRect &r, bool on, const QColor &colour)
{
	p->save();
	p->setRenderHint(QPainter::Antialiasing);
	p->setPen(QPen(colour, 1.4));
	p->setBrush(Qt::NoBrush);

	const QRectF box = QRectF(r).adjusted(2.5, 5.0, -2.5, -5.0);
	QPainterPath lid;
	lid.moveTo(box.left(), box.center().y());
	lid.quadTo(box.center().x(), box.top(), box.right(), box.center().y());
	lid.quadTo(box.center().x(), box.bottom(), box.left(), box.center().y());
	p->drawPath(lid);

	if (on) {
		p->setBrush(colour);
		p->drawEllipse(box.center(), 2.0, 2.0);
	} else {
		/* Struck through: "hidden" must be obvious from across a container,
		 * not a subtle change of shade. */
		p->drawLine(QPointF(r.left() + 3, r.bottom() - 4), QPointF(r.right() - 3, r.top() + 4));
	}
	p->restore();
}

void paintLock(QPainter *p, const QRect &r, bool locked, const QColor &colour)
{
	p->save();
	p->setRenderHint(QPainter::Antialiasing);
	p->setPen(QPen(colour, 1.4));

	const QRectF body(r.left() + 5.0, r.center().y() - 1.0, r.width() - 10.0, r.height() / 2.0 - 1.0);
	p->setBrush(locked ? QBrush(colour) : Qt::NoBrush);
	p->drawRoundedRect(body, 1.5, 1.5);

	/* Shackle: closed when locked, tilted open when not. */
	p->setBrush(Qt::NoBrush);
	const QRectF shackle(body.left() + 2.0, body.top() - body.height() * 0.85, body.width() - 4.0, body.height());
	if (locked) {
		p->drawArc(shackle, 0, 180 * 16);
	} else {
		p->drawArc(shackle.translated(2.0, 0.0), 0, 110 * 16);
	}
	p->restore();
}

} // namespace

MCLayersDelegate::MCLayersDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

QRect MCLayersDelegate::visibilityRect(const QStyleOptionViewItem &option, const QModelIndex &index)
{
	if (!isElement(index)) {
		return {};
	}
	const int top = option.rect.top() + (option.rect.height() - kToggleSize) / 2;
	const int left = option.rect.right() - (kToggleSize * 2) - (kToggleSpacing * 2);
	return QRect(left, top, kToggleSize, kToggleSize);
}

QRect MCLayersDelegate::lockRect(const QStyleOptionViewItem &option, const QModelIndex &index)
{
	if (!isElement(index)) {
		return {};
	}
	const int top = option.rect.top() + (option.rect.height() - kToggleSize) / 2;
	const int left = option.rect.right() - kToggleSize - kToggleSpacing;
	return QRect(left, top, kToggleSize, kToggleSize);
}

void MCLayersDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	QStyleOptionViewItem opt = option;
	initStyleOption(&opt, index);

	/* Text and icon are drawn here, so let the style paint only the
	 * background, selection and focus ring. */
	opt.text.clear();
	opt.icon = QIcon();
	opt.features &= ~QStyleOptionViewItem::HasDecoration;
	QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
	style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

	const bool element = isElement(index);
	const bool visible = !element || index.data(MCLayersModel::VisibleRole).toBool();
	const bool locked = element && index.data(MCLayersModel::LockedRole).toBool();
	const bool program = !element && index.data(MCLayersModel::ProgramRole).toBool();
	const bool signalLost = element && index.data(MCLayersModel::SignalLostRole).toBool();

	const bool selected = opt.state & QStyle::State_Selected;
	QColor fg = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);

	/* A hidden Element is dimmed. It stays legible -- an operator needs to see
	 * that it exists and is off, not hunt for it. */
	QColor rowFg = fg;
	if (!visible) {
		rowFg.setAlpha(110);
	}

	painter->save();

	int x = opt.rect.left() + 4;
	const int iconSize = 16;
	const int iconTop = opt.rect.top() + (opt.rect.height() - iconSize) / 2;

	if (const QIcon icon = iconFor(index); !icon.isNull()) {
		icon.paint(painter, QRect(x, iconTop, iconSize, iconSize), Qt::AlignCenter, QIcon::Normal,
			   visible ? QIcon::On : QIcon::Off);
	}
	x += iconSize + 6;

	/* The program Canvas gets a marker so the operator always knows what is
	 * live -- readable from a distance, which a subtle highlight is not. */
	if (program) {
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(Qt::NoPen);
		painter->setBrush(QColor(220, 60, 60));
		painter->drawEllipse(QPointF(x + 4, opt.rect.center().y() + 1), 4.0, 4.0);
		painter->restore();
		x += 14;
	}

	/*
	 * A camera that has stopped sending gets a red dot and its name in red.
	 * Neither backend announces the loss -- libobs keeps rendering the last
	 * frame -- so without this the tree shows a healthy-looking Element for a
	 * camera that died ten minutes ago.
	 */
	if (signalLost) {
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(Qt::NoPen);
		painter->setBrush(QColor(220, 60, 60));
		painter->drawEllipse(QPointF(x + 4, opt.rect.center().y() + 1), 4.0, 4.0);
		painter->restore();
		x += 14;

		rowFg = QColor(230, 90, 90);
	}

	const int rightReserve = element ? (kToggleSize * 2 + kToggleSpacing * 3) : 4;
	const QRect textRect(x, opt.rect.top(), opt.rect.right() - x - rightReserve, opt.rect.height());

	QFont font = opt.font;
	font.setBold(!element);
	painter->setFont(font);
	painter->setPen(rowFg);
	painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
			  painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
							    textRect.width()));

	if (element) {
		QColor toggleColour = fg;
		toggleColour.setAlpha(visible ? 210 : 120);
		paintEye(painter, visibilityRect(opt, index), visible, toggleColour);

		QColor lockColour = fg;
		lockColour.setAlpha(locked ? 210 : 110);
		paintLock(painter, lockRect(opt, index), locked, lockColour);
	}

	painter->restore();
}

QSize MCLayersDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	QSize size = QStyledItemDelegate::sizeHint(option, index);
	/* Tall enough for a gloved finger on a trackball, and for the toggles. */
	size.setHeight(std::max(size.height(), kToggleSize + 8));
	return size;
}

void MCLayersDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
					    const QModelIndex &index) const
{
	QRect r = option.rect;
	if (isElement(index)) {
		r.setRight(r.right() - (kToggleSize * 2 + kToggleSpacing * 3));
	}
	r.setLeft(r.left() + 26);
	editor->setGeometry(r);
}
