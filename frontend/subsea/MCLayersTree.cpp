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

#include "MCLayersTree.hpp"
#include "MCLayersDelegate.hpp"
#include "MCLayersModel.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScopedValueRollback>

#include "moc_MCLayersTree.cpp"

MCLayersTree::MCLayersTree(QWidget *parent) : QTreeView(parent)
{
	model_ = new MCLayersModel(this);
	setModel(model_);
	setItemDelegate(new MCLayersDelegate(this));

	setObjectName(QStringLiteral("layersTree"));
	setHeaderHidden(true);
	setRootIsDecorated(true);
	setUniformRowHeights(true);
	setExpandsOnDoubleClick(false);
	setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setContextMenuPolicy(Qt::DefaultContextMenu);

	/* InternalMove would make Qt remove the dragged rows itself; the model
	 * mutates libobs and lets the resulting signals rebuild the rows instead. */
	setDragDropMode(QAbstractItemView::DragDrop);
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setDefaultDropAction(Qt::MoveAction);

	/* Everything starts expanded: seeing all Canvases and their Elements at
	 * once is the reason this panel exists. */
	expandAll();
	connect(model_, &QAbstractItemModel::modelReset, this, [this]() { expandAll(); });
	connect(model_, &QAbstractItemModel::rowsInserted, this, [this]() { expandAll(); });

	connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &MCLayersTree::onSelectionChanged);
}

// MARK: - Selection

void MCLayersTree::onSelectionChanged()
{
	if (settingSelection_) {
		return;
	}

	const QModelIndex index = currentIndex();
	if (!index.isValid()) {
		return;
	}

	const QScopedValueRollback<bool> guard(settingSelection_, true);

	if (model_->kindOf(index) == MCLayersModel::Kind::Canvas) {
		emit canvasActivated(model_->canvasAt(index));
	} else {
		/* Selecting an Element implies its Canvas: the preview has to be
		 * showing the right Canvas for the selection to mean anything. */
		emit canvasActivated(model_->canvasAt(index));
		emit elementActivated(model_->elementAt(index));
	}
}

void MCLayersTree::selectCanvas(obs_scene_t *scene)
{
	if (settingSelection_) {
		return;
	}

	const QModelIndex index = model_->indexOfCanvas(scene);
	if (!index.isValid()) {
		return;
	}

	const QScopedValueRollback<bool> guard(settingSelection_, true);
	setCurrentIndex(index);
	scrollTo(index, QAbstractItemView::EnsureVisible);
}

void MCLayersTree::selectElement(obs_sceneitem_t *item)
{
	if (settingSelection_ || !item) {
		return;
	}

	const QModelIndex canvasIndex = model_->indexOfCanvas(obs_sceneitem_get_scene(item));
	if (!canvasIndex.isValid()) {
		return;
	}

	for (int row = 0; row < model_->rowCount(canvasIndex); row++) {
		const QModelIndex candidate = model_->index(row, 0, canvasIndex);
		if (model_->elementAt(candidate) == item) {
			const QScopedValueRollback<bool> guard(settingSelection_, true);
			setCurrentIndex(candidate);
			scrollTo(candidate, QAbstractItemView::EnsureVisible);
			return;
		}
	}
}

// MARK: - Input

bool MCLayersTree::handleToggleClick(const QModelIndex &index, const QPoint &pos)
{
	if (!index.isValid() || model_->kindOf(index) != MCLayersModel::Kind::Element) {
		return false;
	}

	QStyleOptionViewItem option;
	initViewItemOption(&option);
	option.rect = visualRect(index);

	if (MCLayersDelegate::visibilityRect(option, index).contains(pos)) {
		model_->toggleVisible(index);
		return true;
	}
	if (MCLayersDelegate::lockRect(option, index).contains(pos)) {
		model_->toggleLocked(index);
		return true;
	}
	return false;
}

void MCLayersTree::mousePressEvent(QMouseEvent *event)
{
	const QModelIndex index = indexAt(event->pos());
	if (event->button() == Qt::LeftButton && handleToggleClick(index, event->pos())) {
		/* Consumed: toggling must not also change selection or start a drag. */
		event->accept();
		return;
	}
	QTreeView::mousePressEvent(event);
}

void MCLayersTree::mouseDoubleClickEvent(QMouseEvent *event)
{
	const QModelIndex index = indexAt(event->pos());
	if (!index.isValid()) {
		QTreeView::mouseDoubleClickEvent(event);
		return;
	}

	/* Never let a double-click on a toggle fall through to rename. */
	if (handleToggleClick(index, event->pos())) {
		event->accept();
		return;
	}

	if (model_->kindOf(index) == MCLayersModel::Kind::Canvas) {
		edit(index);
		return;
	}

	OBSSceneItem item = model_->elementAt(index);
	const QString sourceId = index.data(MCLayersModel::SourceIdRole).toString();
	if (sourceId == QLatin1String("scene")) {
		/* A Canvas used as an Element is an assigned Overlay Template. */
		emit overlayEditRequested(item);
		return;
	}

	/* Task 1.4 connects this to the properties dialog. */
	emit elementActivated(item);
}

void MCLayersTree::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Space: {
		const QModelIndex index = currentIndex();
		if (index.isValid() && model_->kindOf(index) == MCLayersModel::Kind::Element) {
			model_->toggleVisible(index);
			event->accept();
			return;
		}
		break;
	}
	case Qt::Key_Delete:
		removeSelected();
		event->accept();
		return;
	default:
		break;
	}

	QTreeView::keyPressEvent(event);
}

void MCLayersTree::removeSelected()
{
	/* Deliberately not implemented here yet: removal has to go through
	 * OBSBasic so that it lands on the undo stack and gets the same
	 * confirmation as the rest of the app. Task 1.4 wires it up. Doing it
	 * directly against libobs would give the operator an unundoable delete,
	 * which is exactly the wrong failure mode mid-dive. */
	blog(LOG_DEBUG, "[MCLayers] Remove requested; wiring lands in task 1.4");
}

void MCLayersTree::contextMenuEvent(QContextMenuEvent *event)
{
	const QModelIndex index = indexAt(event->pos());

	QMenu menu(this);

	if (!index.isValid()) {
		menu.addAction(QTStr("AddScene"), this, [this]() { emit canvasActivated(nullptr); });
		menu.exec(event->globalPos());
		return;
	}

	const bool element = model_->kindOf(index) == MCLayersModel::Kind::Element;

	if (element) {
		const bool visible = index.data(MCLayersModel::VisibleRole).toBool();
		const bool locked = index.data(MCLayersModel::LockedRole).toBool();

		QAction *visAction = menu.addAction(QTStr("Basic.Main.Sources.Visibility"));
		visAction->setCheckable(true);
		visAction->setChecked(visible);
		connect(visAction, &QAction::triggered, this, [this, index]() { model_->toggleVisible(index); });

		QAction *lockAction = menu.addAction(QTStr("Basic.Main.Sources.Lock"));
		lockAction->setCheckable(true);
		lockAction->setChecked(locked);
		connect(lockAction, &QAction::triggered, this, [this, index]() { model_->toggleLocked(index); });

		menu.addSeparator();
	}

	menu.addAction(QTStr("Rename"), this, [this, index]() { edit(index); });

	/* Remove is intentionally absent until 1.4 routes it through OBSBasic's
	 * undo stack -- see removeSelected(). */

	menu.exec(event->globalPos());
}
