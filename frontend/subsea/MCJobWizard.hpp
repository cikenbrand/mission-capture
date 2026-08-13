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

#include "MCCaptureDevices.hpp"
#include "MCJobMetadata.hpp"

#include <QVector>
#include <QWizard>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

/*
 * The New Job wizard -- what replaces OBS's auto-configuration wizard.
 *
 * WHAT A JOB IS
 * -------------
 * OBS splits the same idea across two menus: a Profile holds encoder and output
 * settings, a Scene Collection holds the scenes, and keeping them in step is
 * the operator's problem. For inspection work they are one thing: a Job. This
 * wizard creates one, so the Profile and Scene Collection menus can retire.
 *
 * WHAT IT DOES NOT DO YET
 * -----------------------
 * The camera page names Canvases and records which device each was meant for,
 * but does not create the Element -- that is Phase 2, which owns the unified
 * capture Element and its properties. The overlay-template and data-device
 * pages are present and disabled, so the shape of the flow is visible and
 * Phases 4 and 5 have somewhere to land.
 */

class MCJobWizard : public QWizard {
	Q_OBJECT

public:
	explicit MCJobWizard(QWidget *parent = nullptr);

	/* Runs the wizard and, if accepted, creates the Job. Returns true if a Job
	 * was created. */
	static bool run(QWidget *parent);

	/*
	 * Creates the File > New Job action and puts it at the top of that menu.
	 *
	 * Built in code rather than added to OBSBasic.ui so the .ui stays byte-for-
	 * byte upstream's -- it is one of the files most likely to conflict on a
	 * merge. Call before MCFeatures::apply(), so the action exists by the time
	 * the flag pass and the UI manifest look for it.
	 */
	static void installMenuAction(class OBSBasic *main);

private:
	enum PageId { Page_Details, Page_Destination, Page_Cameras, Page_Extras };

	/* Everything the wizard collected, applied together on accept so a
	 * half-built Job cannot be left behind. */
	struct Plan {
		MCJobMetadata::Job metadata;
		QString collectionName;
		QString recordingPath;
		QVector<QString> canvasNames;
		/* Both parallel to canvasNames; empty strings where the row had no
		 * detected device, which is allowed. */
		QVector<QString> canvasDevices;
		QVector<QString> canvasSourceIds;
	};

	Plan collect() const;
	static bool create(const Plan &plan);
};
