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

#pragma once

#include <QRect>
#include <QStyledItemDelegate>

/*
 * Row painting for the Layers tree.
 *
 * Draws each row as: [type icon] [name] .......... [eye] [lock]
 *
 * The toggles are painted rather than made child widgets. Upstream's
 * SourceTreeItem gives every row a QWidget with two real QCheckBoxes, which is
 * fine for one flat list of a handful of sources but scales badly: a Layers
 * tree on an eight-camera spread would carry dozens of live widgets, each with
 * its own event handling and style resolution. Painting keeps scrolling smooth
 * and the row count irrelevant; MCLayersTree turns clicks in the hit rectangles
 * into model calls.
 */

class MCLayersDelegate : public QStyledItemDelegate {
	Q_OBJECT

public:
	explicit MCLayersDelegate(QObject *parent = nullptr);

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

	/* Editor geometry has to avoid the toggles, or an inline rename box sits
	 * underneath them. */
	void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
				  const QModelIndex &index) const override;

	/* Hit rectangles, used by the view to route clicks. Both are empty for
	 * Canvas rows, which have no toggles. */
	static QRect visibilityRect(const QStyleOptionViewItem &option, const QModelIndex &index);
	static QRect lockRect(const QStyleOptionViewItem &option, const QModelIndex &index);

	static constexpr int kToggleSize = 20;
	static constexpr int kToggleSpacing = 4;
};
