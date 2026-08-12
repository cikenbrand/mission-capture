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

#include "MCLayersModel.hpp"

#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QDataStream>
#include <QMetaObject>
#include <QListWidget>
#include <QMimeData>

#include <algorithm>
#include <string>

#include "moc_MCLayersModel.cpp"

namespace {

/* Collects a scene's items in libobs order: index 0 first, which is the
 * bottom of the draw order. The row reversal happens in buildElements(). */
bool collectItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *items = static_cast<std::vector<OBSSceneItem> *>(param);

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source || obs_source_removed(source)) {
		return true;
	}

	/* Groups are not part of Mission Capture's element model; skip the group
	 * and its children rather than rendering a third level. See the header. */
	if (obs_sceneitem_is_group(item)) {
		return true;
	}

	items->emplace_back(item);
	return true;
}

bool collectScene(void *param, obs_source_t *source)
{
	auto *scenes = static_cast<std::vector<OBSSource> *>(param);
	if (source && !obs_source_removed(source)) {
		scenes->emplace_back(source);
	}
	return true;
}

} // namespace

MCLayersModel::MCLayersModel(QObject *parent) : QAbstractItemModel(parent)
{
	connectGlobalSignals();
	reload();
}

MCLayersModel::~MCLayersModel() = default;

// MARK: - Structure

void MCLayersModel::reload()
{
	beginResetModel();

	canvases_.clear();

	/* Canvases are the scenes of the main render target. Scenes belonging to
	 * the overlay-template container (Phase 4) live on a different
	 * obs_canvas_t and are deliberately not enumerated here. */
	std::vector<OBSSource> scenes;
	OBSCanvasAutoRelease mainCanvas = obs_get_main_canvas(); /* strong ref */
	obs_canvas_enum_scenes(mainCanvas, collectScene, &scenes);

	/* libobs enumerates in creation order, but the *saved* Canvas order lives
	 * in upstream's Scenes list widget -- SaveSceneListOrder() walks it to
	 * build the "scene_order" array in the Job file. That widget stays alive
	 * behind its feature flag precisely so persistence keeps working
	 * untouched, so the tree mirrors it rather than inventing an order that
	 * would not survive a save. Found by name, so a rename degrades to
	 * creation order rather than breaking. */
	if (OBSBasic *main = OBSBasic::Get()) {
		if (auto *sceneList = main->findChild<QListWidget *>(QStringLiteral("scenes"))) {
			std::vector<std::string> order;
			order.reserve(static_cast<size_t>(sceneList->count()));
			for (int i = 0; i < sceneList->count(); i++) {
				order.push_back(sceneList->item(i)->text().toStdString());
			}

			std::stable_sort(
				scenes.begin(), scenes.end(), [&order](const OBSSource &a, const OBSSource &b) {
					const auto rank = [&order](const OBSSource &s) {
						const char *name = obs_source_get_name(s);
						const auto it = std::find(order.begin(), order.end(), name ? name : "");
						return std::distance(order.begin(), it);
					};
					return rank(a) < rank(b);
				});
		}
	}

	int row = 0;
	for (const OBSSource &sceneSource : scenes) {
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (!scene) {
			continue;
		}

		auto node = std::make_unique<CanvasNode>();
		node->kind = Kind::Canvas;
		node->row = row++;
		node->weakScene = OBSGetWeakRef(sceneSource);
		node->elements = buildElements(scene, node.get());
		connectCanvasSignals(node.get(), scene);

		canvases_.push_back(std::move(node));
	}

	endResetModel();
}

std::vector<std::unique_ptr<MCLayersModel::ElementNode>> MCLayersModel::buildElements(obs_scene_t *scene,
										      CanvasNode *owner) const
{
	std::vector<OBSSceneItem> items;
	obs_scene_enum_items(scene, collectItem, &items);

	/* libobs gave us bottom-first; the tree wants top-first. */
	std::vector<std::unique_ptr<ElementNode>> elements;
	elements.reserve(items.size());

	const int count = static_cast<int>(items.size());
	for (int i = count - 1; i >= 0; i--) {
		auto node = std::make_unique<ElementNode>();
		node->kind = Kind::Element;
		node->row = toRow(i, count);
		node->item = items[static_cast<size_t>(i)];
		node->parent = owner;
		elements.push_back(std::move(node));
	}

	return elements;
}

MCLayersModel::CanvasNode *MCLayersModel::findCanvas(obs_scene_t *scene) const
{
	if (!scene) {
		return nullptr;
	}

	obs_source_t *target = obs_scene_get_source(scene);
	for (const auto &node : canvases_) {
		OBSSourceAutoRelease source = obs_weak_source_get_source(node->weakScene);
		if (source.Get() == target) {
			return node.get();
		}
	}
	return nullptr;
}

void MCLayersModel::refreshCanvasElements(CanvasNode *node)
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(node->weakScene);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		return;
	}

	const QModelIndex parentIndex = createIndex(node->row, 0, node);
	const int oldCount = static_cast<int>(node->elements.size());

	auto rebuilt = buildElements(scene, node);
	const int newCount = static_cast<int>(rebuilt.size());

	/* A reorder keeps the count, so the rows are the same set with different
	 * contents -- dataChanged is both correct and far less disruptive to
	 * selection and scroll position than a remove/insert pair. */
	if (oldCount == newCount) {
		node->elements = std::move(rebuilt);
		if (newCount > 0) {
			emit dataChanged(index(0, 0, parentIndex), index(newCount - 1, 0, parentIndex));
		}
		return;
	}

	/* Counts differ, so rows genuinely appear or disappear. We do not know
	 * which position changed without diffing, so replace the children as one
	 * block: precise enough that Qt keeps the rest of the tree intact, and
	 * still not a whole-model reset. */
	if (oldCount > 0) {
		beginRemoveRows(parentIndex, 0, oldCount - 1);
		node->elements.clear();
		endRemoveRows();
	}

	if (newCount > 0) {
		beginInsertRows(parentIndex, 0, newCount - 1);
		node->elements = std::move(rebuilt);
		endInsertRows();
	}
}

// MARK: - QAbstractItemModel

QModelIndex MCLayersModel::index(int row, int column, const QModelIndex &parent) const
{
	if (column != 0 || row < 0) {
		return {};
	}

	if (!parent.isValid()) {
		if (row >= static_cast<int>(canvases_.size())) {
			return {};
		}
		return createIndex(row, column, canvases_[static_cast<size_t>(row)].get());
	}

	auto *parentNode = static_cast<Node *>(parent.internalPointer());
	if (!parentNode || parentNode->kind != Kind::Canvas) {
		return {};
	}

	auto *canvas = static_cast<CanvasNode *>(parentNode);
	if (row >= static_cast<int>(canvas->elements.size())) {
		return {};
	}
	return createIndex(row, column, canvas->elements[static_cast<size_t>(row)].get());
}

QModelIndex MCLayersModel::parent(const QModelIndex &child) const
{
	if (!child.isValid()) {
		return {};
	}

	auto *node = static_cast<Node *>(child.internalPointer());
	if (!node || node->kind != Kind::Element) {
		return {};
	}

	auto *element = static_cast<ElementNode *>(node);
	if (!element->parent) {
		return {};
	}
	return createIndex(element->parent->row, 0, element->parent);
}

int MCLayersModel::rowCount(const QModelIndex &parent) const
{
	if (!parent.isValid()) {
		return static_cast<int>(canvases_.size());
	}

	auto *node = static_cast<Node *>(parent.internalPointer());
	if (!node || node->kind != Kind::Canvas) {
		return 0;
	}
	return static_cast<int>(static_cast<CanvasNode *>(node)->elements.size());
}

int MCLayersModel::columnCount(const QModelIndex &) const
{
	return 1;
}

QVariant MCLayersModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid()) {
		return {};
	}

	auto *node = static_cast<Node *>(index.internalPointer());
	if (!node) {
		return {};
	}

	if (role == KindRole) {
		return static_cast<int>(node->kind);
	}

	if (node->kind == Kind::Canvas) {
		auto *canvas = static_cast<CanvasNode *>(node);
		OBSSourceAutoRelease source = obs_weak_source_get_source(canvas->weakScene);
		if (!source) {
			return {};
		}

		switch (role) {
		case Qt::DisplayRole:
			return QString::fromUtf8(obs_source_get_name(source));
		case ProgramRole: {
			/* Not obs_get_output_source(0): channel 0 holds the
			 * *transition*, which in turn holds the scene, so comparing
			 * against it never matches. Ask the frontend instead.
			 *
			 * With Studio Mode disabled the current Canvas is the program
			 * Canvas; if it is ever re-enabled this should consult
			 * GetProgramSource() in that mode. */
			OBSBasic *main = OBSBasic::Get();
			if (!main) {
				return false;
			}
			return main->GetCurrentSceneSource().Get() == source.Get();
		}
		default:
			return {};
		}
	}

	auto *element = static_cast<ElementNode *>(node);
	obs_source_t *source = obs_sceneitem_get_source(element->item);
	if (!source) {
		return {};
	}

	switch (role) {
	case Qt::DisplayRole:
		return QString::fromUtf8(obs_source_get_name(source));
	case VisibleRole:
		return obs_sceneitem_visible(element->item);
	case LockedRole:
		return obs_sceneitem_locked(element->item);
	case SelectedRole:
		return obs_sceneitem_selected(element->item);
	case SourceIdRole:
		return QString::fromUtf8(obs_source_get_id(source));
	default:
		return {};
	}
}

bool MCLayersModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (!index.isValid() || role != Qt::EditRole) {
		return false;
	}

	const QString name = value.toString().trimmed();
	if (name.isEmpty()) {
		return false;
	}

	auto *node = static_cast<Node *>(index.internalPointer());
	if (!node) {
		return false;
	}

	obs_source_t *source = nullptr;
	OBSSourceAutoRelease canvasSource;
	if (node->kind == Kind::Canvas) {
		canvasSource = obs_weak_source_get_source(static_cast<CanvasNode *>(node)->weakScene);
		source = canvasSource;
	} else {
		source = obs_sceneitem_get_source(static_cast<ElementNode *>(node)->item);
	}

	if (!source || name == QString::fromUtf8(obs_source_get_name(source))) {
		return false;
	}

	/* Names must be unique across sources: libobs looks sources up by name, and
	 * a duplicate silently attaches to the wrong object. */
	OBSSourceAutoRelease existing = obs_get_source_by_name(QT_TO_UTF8(name));
	if (existing) {
		blog(LOG_WARNING, "[MCLayers] Rename rejected: '%s' is already in use", QT_TO_UTF8(name));
		return false;
	}

	/* The source_rename signal brings the change back to us, so do not touch
	 * the node here -- that would double-apply it. */
	obs_source_set_name(source, QT_TO_UTF8(name));
	return true;
}

void MCLayersModel::toggleVisible(const QModelIndex &index)
{
	OBSSceneItem item = elementAt(index);
	if (item) {
		obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
	}
}

void MCLayersModel::toggleLocked(const QModelIndex &index)
{
	OBSSceneItem item = elementAt(index);
	if (item) {
		obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
	}
}

// MARK: - Drag and drop

Qt::DropActions MCLayersModel::supportedDropActions() const
{
	return Qt::MoveAction | Qt::CopyAction;
}

QStringList MCLayersModel::mimeTypes() const
{
	return {QStringLiteral("application/x-mission-capture-element")};
}

QMimeData *MCLayersModel::mimeData(const QModelIndexList &indexes) const
{
	/* Carry the Canvas row and Element row rather than a pointer: by the time
	 * the drop is handled the model may have been rebuilt underneath us, and a
	 * stale ElementNode* would be a use-after-free. */
	QByteArray encoded;
	QDataStream stream(&encoded, QIODevice::WriteOnly);

	int count = 0;
	for (const QModelIndex &index : indexes) {
		if (!index.isValid() || kindOf(index) != Kind::Element) {
			continue;
		}
		const QModelIndex parentIndex = parent(index);
		stream << parentIndex.row() << index.row();
		count++;
	}

	if (count == 0) {
		return nullptr;
	}

	auto *data = new QMimeData;
	data->setData(mimeTypes().first(), encoded);
	return data;
}

bool MCLayersModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const
{
	if (!data || !data->hasFormat(mimeTypes().first())) {
		return false;
	}
	/* Elements may only land inside a Canvas, never at the top level. */
	return parent.isValid();
}

bool MCLayersModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int, const QModelIndex &parent)
{
	if (action == Qt::IgnoreAction) {
		return true;
	}
	if (!canDropMimeData(data, action, row, 0, parent)) {
		return false;
	}

	/* The drop target may be a Canvas row or an Element within one. */
	const QModelIndex canvasIndex = (kindOf(parent) == Kind::Canvas) ? parent : this->parent(parent);
	obs_scene_t *targetScene = canvasAt(canvasIndex);
	if (!targetScene) {
		return false;
	}

	QByteArray encoded = data->data(mimeTypes().first());
	QDataStream stream(&encoded, QIODevice::ReadOnly);

	while (!stream.atEnd()) {
		int sourceCanvasRow = -1;
		int sourceElementRow = -1;
		stream >> sourceCanvasRow >> sourceElementRow;

		const QModelIndex fromCanvas = index(sourceCanvasRow, 0);
		if (!fromCanvas.isValid()) {
			continue;
		}
		const QModelIndex fromIndex = index(sourceElementRow, 0, fromCanvas);
		OBSSceneItem item = elementAt(fromIndex);
		if (!item) {
			continue;
		}

		obs_scene_t *sourceScene = canvasAt(fromCanvas);

		if (sourceScene == targetScene) {
			/* Reorder within one Canvas. The view gives us the row to
			 * insert *above*; libobs counts from the bottom, so the
			 * position converts through the same reversal as everywhere
			 * else. */
			const int count = rowCount(canvasIndex);
			const int targetRow = (row < 0) ? count : row;
			const int clamped = std::clamp(targetRow, 0, count - 1);
			obs_sceneitem_set_order_position(item, toLibobsIndex(clamped, count));
			continue;
		}

		/* Across Canvases. Add the same underlying source to the target and
		 * remove the original: a move, not a duplicate, so both Canvases
		 * keep sharing one live capture rather than opening the device
		 * twice. Copy (Ctrl-drag) skips the removal. */
		obs_sceneitem_t *added = obs_scene_add(targetScene, obs_sceneitem_get_source(item));
		if (!added) {
			continue;
		}

		obs_transform_info transform;
		obs_sceneitem_get_info2(item, &transform);
		obs_sceneitem_set_info2(added, &transform);
		obs_sceneitem_set_visible(added, obs_sceneitem_visible(item));
		obs_sceneitem_set_locked(added, obs_sceneitem_locked(item));

		if (action == Qt::MoveAction) {
			obs_sceneitem_remove(item);
		}
	}

	/* item_add / item_remove / reorder signals rebuild the affected Canvases,
	 * so there is nothing to update here. Returning false stops Qt from also
	 * trying to remove the dragged rows itself. */
	return false;
}

Qt::ItemFlags MCLayersModel::flags(const QModelIndex &index) const
{
	if (!index.isValid()) {
		return Qt::NoItemFlags;
	}

	Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;

	auto *node = static_cast<Node *>(index.internalPointer());
	if (node && node->kind == Kind::Element) {
		/* Locked Elements stay selectable -- the operator must be able to
		 * pick one in order to unlock it -- but cannot be dragged. */
		if (!obs_sceneitem_locked(static_cast<ElementNode *>(node)->item)) {
			f |= Qt::ItemIsDragEnabled;
		}
	} else {
		f |= Qt::ItemIsDropEnabled;
	}

	return f;
}

// MARK: - Accessors

MCLayersModel::Kind MCLayersModel::kindOf(const QModelIndex &index) const
{
	auto *node = static_cast<Node *>(index.internalPointer());
	return (node && node->kind == Kind::Element) ? Kind::Element : Kind::Canvas;
}

OBSScene MCLayersModel::canvasAt(const QModelIndex &index) const
{
	if (!index.isValid()) {
		return nullptr;
	}

	auto *node = static_cast<Node *>(index.internalPointer());
	if (!node) {
		return nullptr;
	}

	const CanvasNode *canvas = (node->kind == Kind::Canvas) ? static_cast<CanvasNode *>(node)
								: static_cast<ElementNode *>(node)->parent;
	if (!canvas) {
		return nullptr;
	}

	OBSSourceAutoRelease source = obs_weak_source_get_source(canvas->weakScene);
	return obs_scene_from_source(source);
}

OBSSceneItem MCLayersModel::elementAt(const QModelIndex &index) const
{
	if (!index.isValid()) {
		return nullptr;
	}

	auto *node = static_cast<Node *>(index.internalPointer());
	if (!node || node->kind != Kind::Element) {
		return nullptr;
	}
	return static_cast<ElementNode *>(node)->item;
}

QModelIndex MCLayersModel::indexOfCanvas(obs_scene_t *scene) const
{
	CanvasNode *node = findCanvas(scene);
	if (!node) {
		return {};
	}
	return createIndex(node->row, 0, node);
}

// MARK: - Signal plumbing
//
// Every static handler below runs on a libobs thread and must do nothing except
// marshal to the Qt thread. See the threading note in the header.

void MCLayersModel::connectGlobalSignals()
{
	signal_handler_t *handler = obs_get_signal_handler();

	globalSignals_.emplace_back(handler, "source_create", &MCLayersModel::sourceCreated, this);
	globalSignals_.emplace_back(handler, "source_remove", &MCLayersModel::sourceRemoved, this);
	globalSignals_.emplace_back(handler, "source_destroy", &MCLayersModel::sourceRemoved, this);
	globalSignals_.emplace_back(handler, "source_rename", &MCLayersModel::sourceRenamed, this);
}

void MCLayersModel::connectCanvasSignals(CanvasNode *node, obs_scene_t *scene)
{
	signal_handler_t *handler = obs_source_get_signal_handler(obs_scene_get_source(scene));
	if (!handler) {
		return;
	}

	node->itemSignals.emplace_back(handler, "item_add", &MCLayersModel::itemAdded, this);
	node->itemSignals.emplace_back(handler, "item_remove", &MCLayersModel::itemRemoved, this);
	node->itemSignals.emplace_back(handler, "reorder", &MCLayersModel::itemReordered, this);
	node->itemSignals.emplace_back(handler, "refresh", &MCLayersModel::itemReordered, this);
	node->itemSignals.emplace_back(handler, "item_visible", &MCLayersModel::itemVisible, this);
	node->itemSignals.emplace_back(handler, "item_locked", &MCLayersModel::itemLocked, this);

	/* This is what makes preview selection reach the tree. The preview calls
	 * obs_sceneitem_select(), libobs emits these, and the tree follows -- so
	 * no upstream file has to be modified to keep the two in step. */
	node->itemSignals.emplace_back(handler, "item_select", &MCLayersModel::itemSelected, this);
	node->itemSignals.emplace_back(handler, "item_deselect", &MCLayersModel::itemSelected, this);
}

void MCLayersModel::sourceCreated(void *data, calldata_t *cd)
{
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!source || obs_source_get_type(source) != OBS_SOURCE_TYPE_SCENE) {
		return;
	}
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onCanvasAdded", Qt::QueuedConnection,
				  Q_ARG(OBSSource, OBSSource(source)));
}

void MCLayersModel::sourceRemoved(void *data, calldata_t *cd)
{
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!source || obs_source_get_type(source) != OBS_SOURCE_TYPE_SCENE) {
		return;
	}
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onCanvasRemoved", Qt::QueuedConnection,
				  Q_ARG(OBSSource, OBSSource(source)));
}

void MCLayersModel::sourceRenamed(void *data, calldata_t *cd)
{
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!source) {
		return;
	}
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onCanvasRenamed", Qt::QueuedConnection,
				  Q_ARG(OBSSource, OBSSource(source)));
}

void MCLayersModel::itemAdded(void *data, calldata_t *cd)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onElementsChanged", Qt::QueuedConnection,
				  Q_ARG(OBSScene, OBSScene(scene)));
}

void MCLayersModel::itemRemoved(void *data, calldata_t *cd)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onElementsChanged", Qt::QueuedConnection,
				  Q_ARG(OBSScene, OBSScene(scene)));
}

void MCLayersModel::itemReordered(void *data, calldata_t *cd)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onElementsChanged", Qt::QueuedConnection,
				  Q_ARG(OBSScene, OBSScene(scene)));
}

void MCLayersModel::itemVisible(void *data, calldata_t *cd)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onElementFlagChanged", Qt::QueuedConnection,
				  Q_ARG(OBSScene, OBSScene(scene)), Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void MCLayersModel::itemLocked(void *data, calldata_t *cd)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onElementFlagChanged", Qt::QueuedConnection,
				  Q_ARG(OBSScene, OBSScene(scene)), Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void MCLayersModel::itemSelected(void *data, calldata_t *cd)
{
	auto *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	QMetaObject::invokeMethod(static_cast<MCLayersModel *>(data), "onElementSelectionChanged", Qt::QueuedConnection,
				  Q_ARG(OBSScene, OBSScene(scene)), Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

// MARK: - Qt-thread handlers

void MCLayersModel::onCanvasAdded(OBSSource source)
{
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene || findCanvas(scene)) {
		return;
	}

	/* Only Canvases on the main render target belong in Layers; overlay
	 * templates live on their own container and must not appear. */
	/* Both of these return a *strong* reference, so both must be released --
	 * comparing the raw pointers directly leaks two canvas refs on every scene
	 * creation, and a Job switch creates every scene at once. */
	OBSCanvasAutoRelease sourceCanvas = obs_source_get_canvas(source);
	OBSCanvasAutoRelease mainCanvas = obs_get_main_canvas();
	if (sourceCanvas.Get() != mainCanvas.Get()) {
		return;
	}

	const int row = static_cast<int>(canvases_.size());
	beginInsertRows({}, row, row);

	auto node = std::make_unique<CanvasNode>();
	node->kind = Kind::Canvas;
	node->row = row;
	node->weakScene = OBSGetWeakRef(source);
	node->elements = buildElements(scene, node.get());
	connectCanvasSignals(node.get(), scene);
	canvases_.push_back(std::move(node));

	endInsertRows();
}

void MCLayersModel::onCanvasRemoved(OBSSource source)
{
	obs_scene_t *scene = obs_scene_from_source(source);
	CanvasNode *node = findCanvas(scene);
	if (!node) {
		return;
	}

	const int row = node->row;
	beginRemoveRows({}, row, row);
	canvases_.erase(canvases_.begin() + row);
	/* Rows after the removed one shift up; parent() depends on these. */
	for (size_t i = static_cast<size_t>(row); i < canvases_.size(); i++) {
		canvases_[i]->row = static_cast<int>(i);
	}
	endRemoveRows();
}

void MCLayersModel::onCanvasRenamed(OBSSource source)
{
	obs_scene_t *scene = obs_scene_from_source(source);
	if (CanvasNode *node = findCanvas(scene)) {
		const QModelIndex idx = createIndex(node->row, 0, node);
		emit dataChanged(idx, idx, {Qt::DisplayRole});
		return;
	}

	/* Not a Canvas -- an Element's underlying source was renamed. Refresh any
	 * row showing it. Cheap: a handful of Canvases with a few Elements each. */
	for (const auto &canvas : canvases_) {
		for (const auto &element : canvas->elements) {
			if (obs_sceneitem_get_source(element->item) == source) {
				const QModelIndex idx = createIndex(element->row, 0, element.get());
				emit dataChanged(idx, idx, {Qt::DisplayRole});
			}
		}
	}
}

void MCLayersModel::onElementsChanged(OBSScene scene)
{
	if (CanvasNode *node = findCanvas(scene)) {
		refreshCanvasElements(node);
	}
}

void MCLayersModel::onElementSelectionChanged(OBSScene scene, OBSSceneItem item)
{
	CanvasNode *node = findCanvas(scene);
	if (!node) {
		return;
	}

	for (const auto &element : node->elements) {
		if (element->item == item) {
			const QModelIndex idx = createIndex(element->row, 0, element.get());
			emit dataChanged(idx, idx, {SelectedRole});
			break;
		}
	}

	emit selectionChangedExternally();
}

void MCLayersModel::refreshProgramMarkers()
{
	/* Cheap: a handful of Canvas rows, and only on an actual program change. */
	for (const auto &canvas : canvases_) {
		const QModelIndex idx = createIndex(canvas->row, 0, canvas.get());
		emit dataChanged(idx, idx, {ProgramRole});
	}
}

QModelIndex MCLayersModel::indexOfElement(obs_sceneitem_t *item) const
{
	if (!item) {
		return {};
	}

	for (const auto &canvas : canvases_) {
		for (const auto &element : canvas->elements) {
			if (element->item == item) {
				return createIndex(element->row, 0, element.get());
			}
		}
	}
	return {};
}

void MCLayersModel::onElementFlagChanged(OBSScene scene, OBSSceneItem item)
{
	CanvasNode *node = findCanvas(scene);
	if (!node) {
		return;
	}

	for (const auto &element : node->elements) {
		if (element->item == item) {
			const QModelIndex idx = createIndex(element->row, 0, element.get());
			emit dataChanged(idx, idx, {VisibleRole, LockedRole});
			return;
		}
	}
}
