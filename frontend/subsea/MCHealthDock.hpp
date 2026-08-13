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

#include <QWidget>

class QTableWidget;
class QTimer;

/*
 * "Is every camera still working?" -- one table, answerable at a glance.
 *
 * WHAT THIS REPLACES
 * ------------------
 * Upstream's Stats dock, hidden since task 0.4 and marked Rework in the UI
 * audit. Stats reports CPU, memory, disk and per-output dropped frames, all of
 * which describe the *machine*. None of it answers the question an inspection
 * operator actually has, which is about the cameras.
 *
 * The audit deferred building this until 2.6 precisely so it could be built
 * once, against real per-Element health, rather than twice.
 *
 * WHY MEASURED FRAME RATE IS THE HEADLINE
 * ---------------------------------------
 * A camera configured for 25 fps and delivering 12 looks completely normal in
 * the preview -- the picture is live, just juddering in a way nobody notices
 * on silt and slow pans. It is the failure most likely to be discovered during
 * review rather than during the dive, and the only way to see it is to count.
 *
 * Latency is deliberately not claimed here: see OI-63.
 */

class MCHealthDock : public QWidget {
	Q_OBJECT

public:
	explicit MCHealthDock(QWidget *parent = nullptr);

	/* Creates the dock, registers it beside the others and returns it. */
	static class OBSDock *install(class OBSBasic *main);

private slots:
	void refresh();

private:
	QTableWidget *table_ = nullptr;
	QTimer *timer_ = nullptr;
};
