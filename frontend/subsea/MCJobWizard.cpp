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

#include "MCJobWizard.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWizardPage>

#include "moc_MCJobWizard.cpp"

namespace {

/* Field names, so pages can read each other's values through QWizard. */
constexpr const char *kFieldNumber = "job.number";
constexpr const char *kFieldClient = "job.client";
constexpr const char *kFieldVessel = "job.vessel";
constexpr const char *kFieldSystem = "job.system";
constexpr const char *kFieldNotes = "job.notes";
constexpr const char *kFieldPath = "job.path";

/*
 * A Job name becomes a directory name, so it has to survive the filesystem.
 * Kept readable rather than slugged to death -- an operator looking at a folder
 * listing should recognise their own job number.
 */
QString sanitiseName(const QString &raw)
{
	QString out = raw;
	out.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("-"));
	out = out.trimmed();
	while (out.endsWith('.')) {
		out.chop(1);
	}
	return out;
}

QString humanBytes(uint64_t bytes)
{
	constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
	return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / kGiB, 0, 'f', 1);
}

/* --- Page 1: what the job is ------------------------------------------- */

class DetailsPage : public QWizardPage {
public:
	explicit DetailsPage(QWidget *parent = nullptr) : QWizardPage(parent)
	{
		setTitle(QTStr("JobWizard.Details.Title"));
		setSubTitle(QTStr("JobWizard.Details.SubTitle"));

		auto *number = new QLineEdit(this);
		auto *client = new QLineEdit(this);
		auto *vessel = new QLineEdit(this);
		auto *system = new QLineEdit(this);
		auto *notes = new QPlainTextEdit(this);
		notes->setMaximumHeight(70);

		number->setObjectName(QStringLiteral("jobNumber"));
		client->setObjectName(QStringLiteral("jobClient"));
		vessel->setObjectName(QStringLiteral("jobVessel"));
		system->setObjectName(QStringLiteral("jobSystem"));

		/* The job number is the only mandatory field: it names the Job, and
		 * everything downstream -- the folder, the filenames, the Phase 8
		 * manifest -- is identified by it. The rest is paperwork that can be
		 * filled in later without breaking anything. */
		registerField(QString("%1*").arg(kFieldNumber), number);
		registerField(kFieldClient, client);
		registerField(kFieldVessel, vessel);
		registerField(kFieldSystem, system);
		registerField(kFieldNotes, notes, "plainText", SIGNAL(textChanged()));

		auto *form = new QFormLayout(this);
		form->addRow(QTStr("JobWizard.Details.Number"), number);
		form->addRow(QTStr("JobWizard.Details.Client"), client);
		form->addRow(QTStr("JobWizard.Details.Vessel"), vessel);
		form->addRow(QTStr("JobWizard.Details.System"), system);
		form->addRow(QTStr("JobWizard.Details.Notes"), notes);
	}
};

/* --- Page 2: where the footage goes ------------------------------------- */

class DestinationPage : public QWizardPage {
public:
	explicit DestinationPage(QWidget *parent = nullptr) : QWizardPage(parent)
	{
		setTitle(QTStr("JobWizard.Destination.Title"));
		setSubTitle(QTStr("JobWizard.Destination.SubTitle"));

		path_ = new QLineEdit(this);
		path_->setObjectName(QStringLiteral("jobPath"));
		registerField(QString("%1*").arg(kFieldPath), path_);

		auto *browse = new QPushButton(QTStr("Browse"), this);
		browse->setObjectName(QStringLiteral("jobPathBrowse"));

		free_ = new QLabel(this);
		free_->setObjectName(QStringLiteral("jobFreeSpace"));

		auto *row = new QHBoxLayout;
		row->addWidget(path_);
		row->addWidget(browse);

		auto *layout = new QVBoxLayout(this);
		layout->addWidget(new QLabel(QTStr("JobWizard.Destination.Path"), this));
		layout->addLayout(row);
		layout->addWidget(free_);
		layout->addStretch();

		connect(browse, &QPushButton::clicked, this, [this]() {
			const QString chosen = QFileDialog::getExistingDirectory(
				this, QTStr("JobWizard.Destination.Title"), path_->text());
			if (!chosen.isEmpty()) {
				path_->setText(QDir::toNativeSeparators(chosen));
			}
		});

		connect(path_, &QLineEdit::textChanged, this, &DestinationPage::updateFreeSpace);
	}

	void initializePage() override
	{
		if (path_->text().isEmpty()) {
			OBSBasic *main = OBSBasic::Get();
			const char *existing = main ? config_get_string(main->Config(), "SimpleOutput", "FilePath")
						    : nullptr;
			path_->setText(QDir::toNativeSeparators(QString::fromUtf8(existing ? existing : "")));
		}
		updateFreeSpace();
	}

private:
	void updateFreeSpace()
	{
		const QString dir = path_->text();
		if (dir.isEmpty() || !QDir(dir).exists()) {
			free_->setText(QTStr("JobWizard.Destination.NoSuchFolder"));
			return;
		}

		const uint64_t bytes = os_get_free_disk_space(QT_TO_UTF8(dir));

		/*
		 * A rough hours figure, not a promise. Free space is the failure that
		 * ends a dive silently, and "412 GB" means nothing to someone deciding
		 * whether to start a six-hour run -- so it is stated in the unit the
		 * decision is actually made in, with the assumption spelled out.
		 */
		constexpr double kBytesPerHour = 25.0 * 1024.0 * 1024.0 * 1024.0;
		const double hours = static_cast<double>(bytes) / kBytesPerHour;

		free_->setText(
			QTStr("JobWizard.Destination.Free").arg(humanBytes(bytes)).arg(QString::number(hours, 'f', 1)));
	}

	QLineEdit *path_ = nullptr;
	QLabel *free_ = nullptr;
};

/* --- Page 3: the cameras ------------------------------------------------ */

class CamerasPage : public QWizardPage {
public:
	explicit CamerasPage(QWidget *parent = nullptr) : QWizardPage(parent)
	{
		setTitle(QTStr("JobWizard.Cameras.Title"));
		setSubTitle(QTStr("JobWizard.Cameras.SubTitle"));

		table_ = new QTableWidget(this);
		table_->setObjectName(QStringLiteral("jobCameras"));
		table_->setColumnCount(2);
		table_->setHorizontalHeaderLabels({QTStr("JobWizard.Cameras.Name"), QTStr("JobWizard.Cameras.Device")});
		table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		table_->verticalHeader()->setVisible(false);

		auto *add = new QPushButton(QTStr("JobWizard.Cameras.Add"), this);
		auto *remove = new QPushButton(QTStr("JobWizard.Cameras.Remove"), this);
		add->setObjectName(QStringLiteral("jobCameraAdd"));
		remove->setObjectName(QStringLiteral("jobCameraRemove"));

		note_ = new QLabel(this);
		note_->setWordWrap(true);

		auto *buttons = new QHBoxLayout;
		buttons->addWidget(add);
		buttons->addWidget(remove);
		buttons->addStretch();

		auto *layout = new QVBoxLayout(this);
		layout->addWidget(table_);
		layout->addLayout(buttons);
		layout->addWidget(note_);

		connect(add, &QPushButton::clicked, this, [this]() { addRow(QTStr("JobWizard.Cameras.Unnamed"), {}); });
		connect(remove, &QPushButton::clicked, this, [this]() {
			const int row = table_->currentRow();
			if (row >= 0) {
				table_->removeRow(row);
			}
		});
	}

	void initializePage() override
	{
		if (table_->rowCount() > 0) {
			return;
		}

		devices_ = MCCaptureDevices::enumerate();

		/*
		 * Pre-fill one row per detected device with a generic name. Naming is
		 * the point of this page -- "Pilot Cam" is what appears in Layers and
		 * in every filename -- but a sensible starting row means an operator in
		 * a hurry can press Next and still get a working Job.
		 */
		for (const MCCaptureDevices::Device &device : devices_) {
			addRow(device.name, device.id);
		}

		if (devices_.isEmpty()) {
			/* Not an error. A machine can be set up before the hardware is
			 * fitted, and the Job is still worth creating. */
			addRow(QTStr("JobWizard.Cameras.Unnamed"), {});
			note_->setText(QTStr("JobWizard.Cameras.NoneDetected"));
		} else {
			note_->setText(QTStr("JobWizard.Cameras.Detected").arg(devices_.size()));
		}
	}

	QVector<QString> names() const
	{
		QVector<QString> out;
		for (int row = 0; row < table_->rowCount(); row++) {
			const QTableWidgetItem *item = table_->item(row, 0);
			const QString name = item ? item->text().trimmed() : QString();
			if (!name.isEmpty()) {
				out.append(name);
			}
		}
		return out;
	}

	QVector<QString> deviceIds() const
	{
		QVector<QString> out;
		for (int row = 0; row < table_->rowCount(); row++) {
			const QTableWidgetItem *nameItem = table_->item(row, 0);
			if (!nameItem || nameItem->text().trimmed().isEmpty()) {
				continue;
			}
			const QTableWidgetItem *deviceItem = table_->item(row, 1);
			out.append(deviceItem ? deviceItem->data(Qt::UserRole).toString() : QString());
		}
		return out;
	}

private:
	void addRow(const QString &name, const QString &deviceId)
	{
		const int row = table_->rowCount();
		table_->insertRow(row);

		table_->setItem(row, 0, new QTableWidgetItem(name));

		auto *deviceItem =
			new QTableWidgetItem(deviceId.isEmpty() ? QTStr("JobWizard.Cameras.NoDevice") : name);
		deviceItem->setData(Qt::UserRole, deviceId);
		deviceItem->setFlags(deviceItem->flags() & ~Qt::ItemIsEditable);
		table_->setItem(row, 1, deviceItem);
	}

	QTableWidget *table_ = nullptr;
	QLabel *note_ = nullptr;
	QVector<MCCaptureDevices::Device> devices_;
};

/* --- Page 4: the parts other phases own --------------------------------- */

class ExtrasPage : public QWizardPage {
public:
	explicit ExtrasPage(QWidget *parent = nullptr) : QWizardPage(parent)
	{
		setTitle(QTStr("JobWizard.Extras.Title"));
		setSubTitle(QTStr("JobWizard.Extras.SubTitle"));

		/*
		 * Present and disabled on purpose. The plan puts the overlay template
		 * and the data device in this flow, and showing where they will go is
		 * more honest than a wizard that silently grows two pages later --
		 * an operator who has seen this knows the feature is coming rather
		 * than wondering whether they missed it.
		 */
		auto *overlay = new QLabel(QTStr("JobWizard.Extras.Overlay"), this);
		auto *data = new QLabel(QTStr("JobWizard.Extras.Data"), this);
		overlay->setObjectName(QStringLiteral("jobOverlayStub"));
		data->setObjectName(QStringLiteral("jobDataStub"));
		overlay->setEnabled(false);
		data->setEnabled(false);
		overlay->setWordWrap(true);
		data->setWordWrap(true);

		auto *layout = new QVBoxLayout(this);
		layout->addWidget(overlay);
		layout->addWidget(data);
		layout->addStretch();
	}
};

} // namespace

MCJobWizard::MCJobWizard(QWidget *parent) : QWizard(parent)
{
	setObjectName(QStringLiteral("jobWizard"));
	setWindowTitle(QTStr("JobWizard.Title"));
	setWizardStyle(QWizard::ModernStyle);
	setOption(QWizard::NoBackButtonOnStartPage, true);

	setPage(Page_Details, new DetailsPage(this));
	setPage(Page_Destination, new DestinationPage(this));
	setPage(Page_Cameras, new CamerasPage(this));
	setPage(Page_Extras, new ExtrasPage(this));

	resize(640, 480);
}

MCJobWizard::Plan MCJobWizard::collect() const
{
	Plan plan;

	plan.metadata.number = field(kFieldNumber).toString().trimmed();
	plan.metadata.client = field(kFieldClient).toString().trimmed();
	plan.metadata.vessel = field(kFieldVessel).toString().trimmed();
	plan.metadata.system = field(kFieldSystem).toString().trimmed();
	plan.metadata.notes = field(kFieldNotes).toString().trimmed();
	plan.metadata.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	plan.collectionName = sanitiseName(plan.metadata.number);
	plan.recordingPath = field(kFieldPath).toString().trimmed();

	/* static_cast, not qobject_cast: these pages are file-local and carry no
	 * Q_OBJECT. The id-to-type mapping is fixed in the constructor a few lines
	 * above, so the type is known here without needing the meta-object. */
	if (const auto *cameras = static_cast<const CamerasPage *>(page(Page_Cameras))) {
		plan.canvasNames = cameras->names();
		plan.canvasDevices = cameras->deviceIds();
	}

	return plan;
}

bool MCJobWizard::create(const Plan &plan)
{
	OBSBasic *main = OBSBasic::Get();
	if (!main || plan.collectionName.isEmpty()) {
		return false;
	}

	/* Creating the collection also switches to it, so everything after this
	 * point applies to the new Job. */
	if (!obs_frontend_add_scene_collection(QT_TO_UTF8(plan.collectionName))) {
		blog(LOG_WARNING, "[MCJobWizard] Could not create Job '%s'", QT_TO_UTF8(plan.collectionName));
		return false;
	}

	MCJobMetadata::setCurrent(plan.metadata);

	if (!plan.recordingPath.isEmpty()) {
		/* Both output modes, so the destination holds if Phase 6 switches to
		 * Advanced. Written as an operator value, not a default -- this is a
		 * choice someone made in the wizard. */
		config_set_string(main->Config(), "SimpleOutput", "FilePath", QT_TO_UTF8(plan.recordingPath));
		config_set_string(main->Config(), "AdvOut", "RecFilePath", QT_TO_UTF8(plan.recordingPath));
		config_save_safe(main->Config(), "tmp", nullptr);
	}

	/*
	 * One Canvas per camera. The Element that fills it is Phase 2's -- the
	 * device id each Canvas was named for is stamped into the Job metadata so
	 * task 2.1 can offer it rather than making the operator pick twice.
	 */
	for (int i = 0; i < plan.canvasNames.size(); i++) {
		const QString &name = plan.canvasNames[i];
		OBSSceneAutoRelease scene = obs_scene_create(QT_TO_UTF8(name));
		if (!scene) {
			blog(LOG_WARNING, "[MCJobWizard] Could not create Canvas '%s'", QT_TO_UTF8(name));
			continue;
		}
	}

	blog(LOG_INFO, "[MCJobWizard] Created Job '%s' with %d Canvas(es), recording to '%s'",
	     QT_TO_UTF8(plan.collectionName), static_cast<int>(plan.canvasNames.size()),
	     QT_TO_UTF8(plan.recordingPath));

	obs_frontend_save();
	return true;
}

bool MCJobWizard::run(QWidget *parent)
{
	MCJobWizard wizard(parent);
	if (wizard.exec() != QDialog::Accepted) {
		return false;
	}

	return create(wizard.collect());
}

void MCJobWizard::installMenuAction(OBSBasic *main)
{
	if (!main) {
		return;
	}

	auto *menu = main->findChild<QMenu *>(QStringLiteral("menu_File"));
	if (!menu) {
		blog(LOG_WARNING, "[MCJobWizard] menu_File not found; New Job is unreachable");
		return;
	}

	auto *action = new QAction(QTStr("JobWizard.Title"), main);
	action->setObjectName(QStringLiteral("actionNewJob"));

	QObject::connect(action, &QAction::triggered, main, [main]() { MCJobWizard::run(main); });

	/* First entry, above Show Recordings: starting a Job is the first thing
	 * that happens on a vessel, and it should not be hunted for. */
	const QList<QAction *> existing = menu->actions();
	menu->insertAction(existing.isEmpty() ? nullptr : existing.first(), action);
	menu->insertSeparator(existing.isEmpty() ? nullptr : existing.first());
}
