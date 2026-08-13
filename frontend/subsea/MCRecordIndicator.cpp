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

#include "MCRecordIndicator.hpp"
#include "MCRecordLock.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

#include "moc_MCRecordIndicator.cpp"

namespace {

/*
 * Recording state, kept in one place because the confirm-on-stop threshold
 * needs the elapsed figure from a completely different call path and should not
 * have to find a widget to ask.
 */
struct State {
	bool recording = false;
	bool paused = false;
	QElapsedTimer since;      /* Restarted whenever recording resumes */
	qint64 accumulatedMs = 0; /* Completed stretches, so pauses do not count */
};

State state;

/*
 * Built from a colour rather than patched afterwards: the sheet contains two
 * hex values and a naive replace would have recoloured the white text along
 * with the background.
 */
QString styleFor(const char *background)
{
	return QStringLiteral("QLabel {"
			      "  background-color: %1;"
			      "  color: #ffffff;"
			      "  font-size: 15px;"
			      "  font-weight: bold;"
			      "  letter-spacing: 1px;"
			      "  border-radius: 3px;"
			      "  padding: 6px 4px;"
			      "}")
		.arg(QLatin1String(background));
}

constexpr const char *kRecordingRed = "#c62828";
constexpr const char *kPausedAmber = "#ef6c00";

QString formatElapsed(qint64 seconds)
{
	const qint64 h = seconds / 3600;
	const qint64 m = (seconds % 3600) / 60;
	const qint64 s = seconds % 60;
	return QStringLiteral("%1:%2:%3")
		.arg(h, 2, 10, QLatin1Char('0'))
		.arg(m, 2, 10, QLatin1Char('0'))
		.arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

MCRecordIndicator::MCRecordIndicator(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("recordIndicator"));

	label_ = new QLabel(this);
	label_->setObjectName(QStringLiteral("recordIndicatorLabel"));
	label_->setAlignment(Qt::AlignCenter);

	/*
	 * Styled here rather than in the theme. The themes are the operator's
	 * choice and several are low-contrast by design; this one element has to
	 * look the same in all of them, because its whole job is to be unmissable.
	 */
	label_->setStyleSheet(styleFor(kRecordingRed));

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 4);
	layout->addWidget(label_);

	/* One second is enough for a clock read to the second, and cheap. */
	timer_ = new QTimer(this);
	timer_->setInterval(1000);
	connect(timer_, &QTimer::timeout, this, &MCRecordIndicator::tick);

	obs_frontend_add_event_callback(&MCRecordIndicator::frontendEvent, this);

	refresh();
}

MCRecordIndicator::~MCRecordIndicator()
{
	obs_frontend_remove_event_callback(&MCRecordIndicator::frontendEvent, this);
}

void MCRecordIndicator::frontendEvent(enum obs_frontend_event event, void *data)
{
	auto *self = static_cast<MCRecordIndicator *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		QMetaObject::invokeMethod(self, [self]() { self->setRecording(true); }, Qt::QueuedConnection);
		break;

	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		QMetaObject::invokeMethod(self, [self]() { self->setRecording(false); }, Qt::QueuedConnection);
		break;

	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		QMetaObject::invokeMethod(self, [self]() { self->setPaused(true); }, Qt::QueuedConnection);
		break;

	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		QMetaObject::invokeMethod(self, [self]() { self->setPaused(false); }, Qt::QueuedConnection);
		break;

	default:
		break;
	}
}

void MCRecordIndicator::setRecording(bool recording)
{
	if (recording) {
		state.accumulatedMs = 0;
		state.paused = false;
		state.since.restart();
		timer_->start();
	} else {
		state.accumulatedMs = elapsedSeconds() * 1000;
		timer_->stop();
		/* Releasing the lock was a decision about *this* recording; it must
		 * not carry silently into the next dive. */
		MCRecordLock::onRecordingStopped();
	}

	state.recording = recording;
	refresh();
}

void MCRecordIndicator::setPaused(bool paused)
{
	if (paused == state.paused) {
		return;
	}

	if (paused) {
		/* Bank what has run so far; the clock stops until we resume. */
		state.accumulatedMs += state.since.isValid() ? state.since.elapsed() : 0;
	} else {
		state.since.restart();
	}

	state.paused = paused;
	refresh();
}

void MCRecordIndicator::tick()
{
	refresh();
}

void MCRecordIndicator::refresh()
{
	/* Hidden outright when idle. A permanently visible badge that merely
	 * changes colour is one an operator stops seeing. */
	setVisible(state.recording);

	if (!state.recording) {
		return;
	}

	label_->setText(state.paused ? QTStr("RecordIndicator.Paused").arg(formatElapsed(elapsedSeconds()))
				     : QTStr("RecordIndicator.Recording").arg(formatElapsed(elapsedSeconds())));

	/* Paused is amber, not red: it must not read as "recording" at a glance,
	 * and must not read as "stopped" either. */
	label_->setStyleSheet(styleFor(state.paused ? kPausedAmber : kRecordingRed));
}

qint64 MCRecordIndicator::elapsedSeconds()
{
	if (!state.recording) {
		return 0;
	}

	qint64 ms = state.accumulatedMs;
	if (!state.paused && state.since.isValid()) {
		ms += state.since.elapsed();
	}

	return ms / 1000;
}

bool MCRecordIndicator::isRecording()
{
	return state.recording;
}
