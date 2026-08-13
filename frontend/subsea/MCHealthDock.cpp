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

#include "MCHealthDock.hpp"
#include "MCSignalWatch.hpp"

#include <OBSApp.hpp>
#include <docks/OBSDock.hpp>
#include <qt-wrappers.hpp>
#include <widgets/OBSBasic.hpp>

#include <QHeaderView>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "moc_MCHealthDock.cpp"

namespace {

QColor colourFor(MCSignalWatch::State state)
{
	switch (state) {
	case MCSignalWatch::State::Receiving:
		return QColor(76, 175, 80);
	case MCSignalWatch::State::Lost:
		return QColor(198, 40, 40);
	default:
		return QColor(150, 150, 150);
	}
}

QString labelFor(MCSignalWatch::State state)
{
	switch (state) {
	case MCSignalWatch::State::Receiving:
		return QTStr("Health.State.Receiving");
	case MCSignalWatch::State::Lost:
		return QTStr("Health.State.Lost");
	default:
		return QTStr("Health.State.Waiting");
	}
}

} // namespace

MCHealthDock::MCHealthDock(QWidget *parent) : QWidget(parent)
{
	setObjectName(QStringLiteral("healthPanel"));

	table_ = new QTableWidget(this);
	table_->setObjectName(QStringLiteral("healthTable"));
	table_->setColumnCount(3);
	table_->setHorizontalHeaderLabels(
		{QTStr("Health.Column.Element"), QTStr("Health.Column.State"), QTStr("Health.Column.Fps")});
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table_->verticalHeader()->setVisible(false);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setSelectionMode(QAbstractItemView::NoSelection);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(table_);

	/* One second, matching the watch's own sampling. Faster would show noise
	 * from the measurement rather than from the camera. */
	timer_ = new QTimer(this);
	timer_->setInterval(1000);
	connect(timer_, &QTimer::timeout, this, &MCHealthDock::refresh);
	timer_->start();

	refresh();
}

void MCHealthDock::refresh()
{
	/* Skipped while hidden. This runs every second for the life of the
	 * application, and a dock nobody has opened should cost nothing. */
	if (!isVisible()) {
		return;
	}

	const QVector<MCSignalWatch::Status> statuses = MCSignalWatch::statuses();

	if (table_->rowCount() != statuses.size()) {
		table_->setRowCount(statuses.size());
	}

	for (int row = 0; row < statuses.size(); row++) {
		const MCSignalWatch::Status &s = statuses[row];

		auto set = [&](int column, const QString &text, const QColor &colour = {}) {
			QTableWidgetItem *item = table_->item(row, column);
			if (!item) {
				item = new QTableWidgetItem;
				table_->setItem(row, column, item);
			}
			item->setText(text);
			if (colour.isValid()) {
				item->setForeground(colour);
			}
		};

		set(0, s.elementName);
		set(1, labelFor(s.state), colourFor(s.state));

		/*
		 * Blank rather than "0.0" when nothing is arriving: a zero reads as a
		 * measurement, and there is no measurement to report from a camera
		 * that is not sending.
		 */
		set(2,
		    s.state == MCSignalWatch::State::Receiving ? QString::number(s.fps, 'f', 1) : QStringLiteral("--"),
		    colourFor(s.state));
	}
}

OBSDock *MCHealthDock::install(OBSBasic *main)
{
	if (!main) {
		return nullptr;
	}

	auto *dock = new OBSDock(main);
	dock->setObjectName(QStringLiteral("healthDock"));
	dock->setWindowTitle(QTStr("Health.Title"));
	dock->setWidget(new MCHealthDock(dock));

	main->addDockWidget(Qt::BottomDockWidgetArea, dock);

	/* Hidden by default, like the Stats dock it replaces. An operator opens it
	 * when they want it; the Layers tree already shows a lost camera without
	 * anyone asking. */
	dock->setVisible(false);
	dock->setFloating(true);

	return dock;
}
