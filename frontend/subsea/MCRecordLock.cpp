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

#include "MCRecordLock.hpp"
#include "MCFeatures.hpp"
#include "MCRecordIndicator.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <widgets/OBSBasic.hpp>

#include <util/base.h>

#include <QStatusBar>

namespace MCRecordLock {

namespace {

bool userOverride = false;

} // namespace

bool recording()
{
	return MCRecordIndicator::isRecording();
}

bool locked()
{
	if (!MCFeatures::enabled(MCFeatures::Feature::LockLayersWhileRecording)) {
		return false;
	}

	return recording() && !userOverride;
}

bool overridden()
{
	return userOverride;
}

void setOverridden(bool value)
{
	if (userOverride == value) {
		return;
	}

	userOverride = value;

	/* Logged either way. If an edit during a dive is later questioned, the log
	 * should show the lock was released on purpose and when. */
	blog(LOG_INFO, "[MCRecordLock] Layers editing %s during recording",
	     value ? "UNLOCKED by the operator" : "locked");
}

void onRecordingStopped()
{
	if (userOverride) {
		blog(LOG_INFO, "[MCRecordLock] Recording stopped; clearing the editing override");
		userOverride = false;
	}
}

void explainRefusal(QWidget *context)
{
	OBSBasic *main = OBSBasic::Get();
	if (!main || !main->statusBar()) {
		return;
	}

	Q_UNUSED(context);

	/* Five seconds: long enough to read while doing something else, short
	 * enough not to bury the recording state behind it. */
	main->statusBar()->showMessage(QTStr("RecordLock.Refused"), 5000);
}

} // namespace MCRecordLock
