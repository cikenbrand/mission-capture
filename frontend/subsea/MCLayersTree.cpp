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
#include <widgets/OBSBasic.hpp>
#include <qt-wrappers.hpp>

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QItemSelection>
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

	/* Preview -> tree. The preview calls obs_sceneitem_select(); libobs emits
	 * item_select; the model turns that into this. No upstream file is touched
	 * to keep the two in step, which avoids the merge-fragile seam the plan
	 * expected here. */
	connect(model_, &MCLayersModel::selectionChangedExternally, this, &MCLayersTree::syncSelectionFromLibobs);

	/* Nothing in libobs announces "the program Canvas changed", so the
	 * frontend event has to. Without this the program marker only moves when
	 * the dock happens to repaint for another reason. */
	obs_frontend_add_event_callback(&MCLayersTree::frontendEvent, this);
}

MCLayersTree::~MCLayersTree()
{
	obs_frontend_remove_event_callback(&MCLayersTree::frontendEvent, this);
}

void MCLayersTree::frontendEvent(enum obs_frontend_event event, void *data)
{
	auto *self = static_cast<MCLayersTree *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		QMetaObject::invokeMethod(self, "onProgramCanvasChanged", Qt::QueuedConnection);
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		/* A Job switch tears down every scene and rebuilds it, driving
		 * dozens of create/remove signals through the model at once. Rather
		 * than trust that storm to land in the right order, rebuild once the
		 * dust settles. */
		QMetaObject::invokeMethod(self, "onJobChanged", Qt::QueuedConnection);
		break;

	default:
		break;
	}
}

void MCLayersTree::onJobChanged()
{
	const QScopedValueRollback<bool> guard(settingSelection_, true);
	model_->reload();
	expandAll();
}

void MCLayersTree::onProgramCanvasChanged()
{
	model_->refreshProgramMarkers();

	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	selectCanvas(obs_scene_from_source(current));
}

void MCLayersTree::syncSelectionFromLibobs()
{
	if (settingSelection_) {
		return;
	}

	const QScopedValueRollback<bool> guard(settingSelection_, true);

	/* Mirror libobs' selection state onto the Qt selection. libobs is the
	 * single source of truth: the preview, hotkeys and this tree all write to
	 * it, and all three read back from it. */
	QItemSelection selection;
	for (int c = 0; c < model_->rowCount(); c++) {
		const QModelIndex canvasIndex = model_->index(c, 0);
		for (int e = 0; e < model_->rowCount(canvasIndex); e++) {
			const QModelIndex elementIndex = model_->index(e, 0, canvasIndex);
			if (elementIndex.data(MCLayersModel::SelectedRole).toBool()) {
				selection.select(elementIndex, elementIndex);
			}
		}
	}

	if (selection.isEmpty()) {
		return;
	}

	selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
	selectionModel()->setCurrentIndex(selection.indexes().first(), QItemSelectionModel::NoUpdate);
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

	/* Selecting an Element implies its Canvas -- the preview has to be showing
	 * the right Canvas for the selection to mean anything.
	 *
	 * Switching goes through the public frontend API rather than
	 * OBSBasic::SetCurrentScene(), which is private. That keeps this off the
	 * seam list entirely: obs_frontend_set_current_scene() is a stable public
	 * interface, and reaching into OBSBasic would have meant widening its
	 * header. */
	OBSScene scene = model_->canvasAt(index);
	if (scene) {
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		obs_source_t *wanted = obs_scene_get_source(scene);
		if (current.Get() != wanted) {
			obs_frontend_set_current_scene(wanted);
		}
	}
	emit canvasActivated(scene);

	if (model_->kindOf(index) == MCLayersModel::Kind::Canvas) {
		return;
	}

	/* Tree -> preview. Writing selection into libobs is what makes the
	 * transform gizmos appear on the right Element; the preview reads the same
	 * state we do. */
	const QModelIndexList chosen = selectionModel()->selectedIndexes();
	const QModelIndex canvasIndex = model_->parent(index);

	for (int row = 0; row < model_->rowCount(canvasIndex); row++) {
		const QModelIndex candidate = model_->index(row, 0, canvasIndex);
		OBSSceneItem item = model_->elementAt(candidate);
		if (item) {
			obs_sceneitem_select(item, chosen.contains(candidate));
		}
	}

	emit elementActivated(model_->elementAt(index));
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
	OBSBasic *main = OBSBasic::Get();
	const QModelIndex index = currentIndex();
	if (!main || !index.isValid()) {
		return;
	}

	/* Trigger upstream's own action rather than calling libobs directly. That
	 * one confirms with the user, pushes onto the undo stack, and handles
	 * multi-selection -- an unundoable delete mid-dive is exactly the wrong
	 * failure mode. Found by objectName so a rename degrades to "Delete does
	 * nothing" rather than to a silent unconfirmed delete.
	 *
	 * The actions operate on the current Canvas and its selected items, which
	 * onSelectionChanged() has already written into libobs. */
	const char *actionName = (model_->kindOf(index) == MCLayersModel::Kind::Canvas) ? "actionRemoveScene"
											: "actionRemoveSource";

	if (QAction *action = main->findChild<QAction *>(QString::fromUtf8(actionName))) {
		action->trigger();
	} else {
		blog(LOG_WARNING, "[MCLayers] '%s' not found; Delete did nothing", actionName);
	}
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
