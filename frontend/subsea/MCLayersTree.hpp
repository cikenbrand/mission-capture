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
#include <obs-frontend-api.h>

#include <QTreeView>

class MCLayersModel;
class QMenu;

/*
 * The Layers panel: one tree showing every Canvas and the Elements inside it.
 *
 * Replaces upstream's two separate docks. Task 1.4 retires those and redirects
 * the preview's selection callbacks here; this class is the widget itself.
 *
 * SELECTION
 * ---------
 * Selection can be changed from three directions -- this tree, the preview, and
 * hotkeys -- and each notifies the others. Without a guard that is an infinite
 * loop, so every path that reacts to a selection change goes through
 * settingSelection_. This is the single most likely source of a hang in Phase 1
 * and it is cheaper to prevent than to debug.
 */

class MCLayersTree : public QTreeView {
	Q_OBJECT

public:
	explicit MCLayersTree(QWidget *parent = nullptr);
	~MCLayersTree() override;

	MCLayersModel *layersModel() const { return model_; }

	/* Called by task 1.4 when the preview or a hotkey changes selection. */
	void selectCanvas(obs_scene_t *scene);
	void selectElement(obs_sceneitem_t *item);

signals:
	/* The user picked a Canvas; task 1.4 makes it the program Canvas. */
	void canvasActivated(OBSScene scene);
	/* The user picked an Element; task 1.4 selects it in the preview. */
	void elementActivated(OBSSceneItem item);
	/* Double-clicked an Overlay Element -- Phase 4 opens the editor. */
	void overlayEditRequested(OBSSceneItem item);

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
	void onSelectionChanged();
	/* libobs (usually the preview) changed the selection. */
	void syncSelectionFromLibobs();
	/* The program Canvas changed; repaint markers and follow it. */
	void onProgramCanvasChanged();
	/* A Job switch replaced every scene; rebuild once rather than tracking
	 * the storm of create/remove signals. */
	void onJobChanged();

private:
	static void frontendEvent(enum obs_frontend_event event, void *data);

	/* Returns true if the press landed on a toggle and was consumed. */
	bool handleToggleClick(const QModelIndex &index, const QPoint &pos);

	/* True if editing is locked by a running recording, having said so in the
	 * status bar. Callers should do nothing further when it returns true. */
	bool refuseIfLocked();
	void removeSelected();

	/* Adds one of OBSBasic's own QActions to a context menu, so the entry keeps
	 * upstream's dialog and undo behaviour instead of reimplementing it.
	 * Returns false, and logs, if upstream no longer has that objectName. */
	static bool addUpstreamAction(QMenu &menu, const char *objectName);

	MCLayersModel *model_ = nullptr;
	bool settingSelection_ = false;
};
