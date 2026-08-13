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

#include "MCAddElementDialog.hpp"
#include "MCCaptureDevices.hpp"
#include "MCRtspElement.hpp"
#include "MCVideoCaptureElement.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <util/base.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace MCAddElementDialog {

namespace {

/* Tall enough to hit without aiming, on a trackball, on a moving vessel. */
constexpr int kButtonHeight = 64;

QPushButton *bigButton(QWidget *parent, const QString &title, const QString &subtitle, const QString &objectName)
{
	auto *button = new QPushButton(parent);
	button->setObjectName(objectName);
	button->setMinimumHeight(kButtonHeight);

	/* Subtitle inside the button rather than beside it: the reason to pick
	 * one of these is the detail, not the noun. */
	button->setText(subtitle.isEmpty() ? title : QStringLiteral("%1\n%2").arg(title, subtitle));

	return button;
}

/*
 * Names the Element and picks a device, once the operator has chosen "camera".
 *
 * A second small step rather than a combo box on the picker itself: naming is
 * the part that matters downstream -- it becomes the Layers row, the %CANVAS%
 * token, and the filename -- and burying it in a dropdown gets it skipped.
 */
bool addCaptureDevice(QWidget *parent, obs_scene_t *scene, const QVector<MCCaptureDevices::Device> &devices)
{
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("addCaptureDialog"));
	dialog.setWindowTitle(QTStr("AddElement.Capture.Title"));

	auto *device = new QComboBox(&dialog);
	device->setObjectName(QStringLiteral("addCaptureDevice"));
	for (const MCCaptureDevices::Device &d : devices) {
		/* Backend as a subtitle, so a DeckLink and a webcam of the same
		 * name are still distinguishable, without making the operator care
		 * which is which. */
		device->addItem(QStringLiteral("%1  —  %2").arg(d.name, d.backend), d.id);
	}

	auto *name = new QLineEdit(&dialog);
	name->setObjectName(QStringLiteral("addCaptureName"));
	if (!devices.isEmpty()) {
		name->setText(devices.first().name);
	}

	/* Keep the name in step while the operator browses devices, but stop as
	 * soon as they type -- at that point the name is theirs. */
	auto *edited = new bool(false);
	QObject::connect(name, &QLineEdit::textEdited, &dialog, [edited]() { *edited = true; });
	QObject::connect(device, &QComboBox::currentIndexChanged, &dialog, [=, &devices](int index) {
		if (!*edited && index >= 0 && index < devices.size()) {
			name->setText(devices[index].name);
		}
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QTStr("AddElement.Capture.Device"), &dialog));
	layout->addWidget(device);
	layout->addWidget(new QLabel(QTStr("AddElement.Capture.Name"), &dialog));
	layout->addWidget(name);
	layout->addWidget(buttons);

	const bool accepted = dialog.exec() == QDialog::Accepted;
	const int chosen = device->currentIndex();
	const QString chosenName = name->text().trimmed();
	delete edited;

	if (!accepted || chosen < 0 || chosen >= devices.size() || chosenName.isEmpty()) {
		return false;
	}

	return MCVideoCaptureElement::addTo(scene, devices[chosen], chosenName) != nullptr;
}

} // namespace

bool run(QWidget *parent, obs_scene_t *scene)
{
	if (!scene) {
		return false;
	}

	/* Discovered before the dialog is shown, so the button can say what it
	 * found rather than making the operator open it to learn there is
	 * nothing there. */
	const QVector<MCCaptureDevices::Device> devices = MCCaptureDevices::enumerate();

	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("addElementDialog"));
	dialog.setWindowTitle(QTStr("AddElement.Title"));
	dialog.setMinimumWidth(380);

	auto *capture = bigButton(&dialog, QTStr("AddElement.Capture"),
				  devices.isEmpty() ? QTStr("AddElement.Capture.None")
						    : QTStr("AddElement.Capture.Found").arg(devices.size()),
				  QStringLiteral("addElementCapture"));

	auto *rtsp = bigButton(&dialog, QTStr("AddElement.Rtsp"), QTStr("AddElement.Rtsp.Sub"),
			       QStringLiteral("addElementRtsp"));

	auto *overlay = bigButton(&dialog, QTStr("AddElement.Overlay"), QTStr("AddElement.Overlay.Sub"),
				  QStringLiteral("addElementOverlay"));
	overlay->setEnabled(false);

	/*
	 * A camera with no devices is still worth offering: the operator may be
	 * setting the Job up before the hardware is fitted, and 2.1 makes an
	 * Element with an absent device load and behave correctly. But there is
	 * nothing to choose from, so the button is disabled and says why.
	 */
	capture->setEnabled(!devices.isEmpty());

	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QTStr("AddElement.Prompt"), &dialog));
	layout->addWidget(capture);
	layout->addWidget(rtsp);
	layout->addWidget(overlay);

	auto *cancel = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
	QObject::connect(cancel, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(cancel);

	/* Which button was pressed, resolved after the dialog closes so the
	 * follow-on step is not a dialog parented to a dying one. */
	enum class Choice { None, Capture, Rtsp } choice = Choice::None;

	QObject::connect(capture, &QPushButton::clicked, &dialog, [&]() {
		choice = Choice::Capture;
		dialog.accept();
	});
	QObject::connect(rtsp, &QPushButton::clicked, &dialog, [&]() {
		choice = Choice::Rtsp;
		dialog.accept();
	});

	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}

	switch (choice) {
	case Choice::Capture:
		return addCaptureDevice(parent, scene, devices);
	case Choice::Rtsp:
		return MCRtspElement::promptAndAdd(parent, scene);
	default:
		return false;
	}
}

} // namespace MCAddElementDialog
