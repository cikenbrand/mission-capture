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

#include <obs.hpp>

#include <QAbstractItemModel>

#include <memory>
#include <vector>

/*
 * The model behind the Layers panel.
 *
 * Two levels, and only two:
 *
 *   Canvas   (obs_scene_t)      -- a top-level row
 *     Element (obs_sceneitem_t) -- a child row
 *
 * This replaces upstream's two flat widgets, SceneTree (a QListWidget of
 * scenes) and SourceTree (a QListView of the current scene's items). Showing
 * both levels at once is the point: an inspection engineer with eight cameras
 * should see every Canvas and what is in it without clicking through.
 *
 * ORDERING -- THE THING TO GET RIGHT
 * ----------------------------------
 * libobs enumerates scene items **bottom-first**: index 0 is drawn first and
 * therefore appears *behind* everything else. A layers tree must show the
 * topmost item first, so row 0 is the *last* item libobs enumerates.
 *
 *     row = (count - 1) - libobsIndex
 *
 * The mapping is its own involution, so the same expression converts either
 * way; see toLibobsIndex(). Upstream does the same thing less explicitly, by
 * inserting each enumerated item at position 0 (SourceTreeModel::enumItem).
 * Getting this backwards renders the Z-order upside down, which looks like a
 * rendering bug rather than a model bug -- hence the dedicated assertions.
 *
 * THREADING
 * ---------
 * libobs signals arrive on arbitrary threads: a capture thread, the graphics
 * thread, whichever thread called obs_scene_add. Every handler here does
 * nothing but marshal to the Qt thread via QMetaObject::invokeMethod. Touching
 * the model from a libobs thread is a crash waiting for a busy dive.
 *
 * GROUPS
 * ------
 * OBS scene items can be groups, which would make this a genuine tree rather
 * than two fixed levels. Mission Capture ships three element types and no
 * grouping, so groups are flattened away (their children are not shown) and
 * the structure stays two-deep. The node types are polymorphic so that a
 * recursive model remains possible if that ever changes.
 */

class MCLayersModel : public QAbstractItemModel {
	Q_OBJECT

public:
	enum Roles {
		/* Which level a row is, so a delegate need not guess from depth. */
		KindRole = Qt::UserRole + 1,
		/* Element only: obs_sceneitem_is_visible / _is_locked. */
		VisibleRole,
		LockedRole,
		/* Element only: the source id, e.g. "dshow_input". Drives the icon. */
		SourceIdRole,
		/* Canvas only: true if it is the program Canvas. */
		ProgramRole,
	};

	enum class Kind { Canvas, Element };

	explicit MCLayersModel(QObject *parent = nullptr);
	~MCLayersModel() override;

	/* QAbstractItemModel */
	QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
	QModelIndex parent(const QModelIndex &child) const override;
	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
	Qt::ItemFlags flags(const QModelIndex &index) const override;

	/* Drag and drop. Internal moves only -- there is nothing meaningful to
	 * exchange with another application. */
	Qt::DropActions supportedDropActions() const override;
	QStringList mimeTypes() const override;
	QMimeData *mimeData(const QModelIndexList &indexes) const override;
	bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
			     const QModelIndex &parent) const override;
	bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
			  const QModelIndex &parent) override;

	/* Convenience accessors for the view and for tests. */
	Kind kindOf(const QModelIndex &index) const;
	OBSScene canvasAt(const QModelIndex &index) const;
	OBSSceneItem elementAt(const QModelIndex &index) const;
	QModelIndex indexOfCanvas(obs_scene_t *scene) const;

	/*
	 * Row <-> libobs index conversion. libobs counts from the bottom of the
	 * draw order, the tree counts from the top. Self-inverse.
	 */
	static int toLibobsIndex(int row, int count) { return (count - 1) - row; }
	static int toRow(int libobsIndex, int count) { return (count - 1) - libobsIndex; }

	/* Rebuilds everything. Used on Job load and as the last-resort fallback. */
	void reload();

	/* Convenience mutations the view calls; each ends up back here through a
	 * libobs signal, so the model is never updated speculatively. */
	void toggleVisible(const QModelIndex &index);
	void toggleLocked(const QModelIndex &index);

signals:
	/* Emitted when a drop moved an Element to a different Canvas, so the view
	 * can select it in its new home. */
	void elementMoved(const QModelIndex &newIndex);

private slots:
	/* All of these already run on the Qt thread -- the libobs handlers below
	 * marshal to them. */
	void onCanvasAdded(OBSSource source);
	void onCanvasRemoved(OBSSource source);
	void onCanvasRenamed(OBSSource source);
	void onElementsChanged(OBSScene scene);
	void onElementFlagChanged(OBSScene scene, OBSSceneItem item);

private:
	struct ElementNode;

	struct Node {
		virtual ~Node() = default;
		Kind kind;
		/* Row within the parent. Kept current so parent() is O(1). */
		int row = 0;
	};

	struct CanvasNode : Node {
		OBSWeakSource weakScene;
		std::vector<std::unique_ptr<ElementNode>> elements;
		/* Held so they can be disconnected when the Canvas goes away. */
		std::vector<OBSSignal> itemSignals;
	};

	struct ElementNode : Node {
		OBSSceneItem item;
		CanvasNode *parent = nullptr;
	};

	void connectGlobalSignals();
	void connectCanvasSignals(CanvasNode *node, obs_scene_t *scene);
	std::vector<std::unique_ptr<ElementNode>> buildElements(obs_scene_t *scene, CanvasNode *owner) const;
	CanvasNode *findCanvas(obs_scene_t *scene) const;
	void refreshCanvasElements(CanvasNode *node);

	/* libobs-thread entry points. These do nothing but re-dispatch. */
	static void sourceCreated(void *data, calldata_t *cd);
	static void sourceRemoved(void *data, calldata_t *cd);
	static void sourceRenamed(void *data, calldata_t *cd);
	static void itemAdded(void *data, calldata_t *cd);
	static void itemRemoved(void *data, calldata_t *cd);
	static void itemReordered(void *data, calldata_t *cd);
	static void itemVisible(void *data, calldata_t *cd);
	static void itemLocked(void *data, calldata_t *cd);

	std::vector<std::unique_ptr<CanvasNode>> canvases_;
	std::vector<OBSSignal> globalSignals_;
};
