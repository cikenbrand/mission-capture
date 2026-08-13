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

#include "MCJobMetadata.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/base.h>

#include <QDateTime>

namespace MCJobMetadata {

namespace {

/* Our corner of the collection's "modules" object. Namespaced so it cannot
 * collide with a plugin that stores data under its own name. */
constexpr const char *kModuleKey = "mission-capture";
constexpr const char *kJobKey = "job";

Job currentJob;
bool registered = false;

void readFrom(obs_data_t *modules)
{
	currentJob = Job{};

	if (!modules) {
		return;
	}

	OBSDataAutoRelease ours = obs_data_get_obj(modules, kModuleKey);
	if (!ours) {
		return;
	}

	OBSDataAutoRelease job = obs_data_get_obj(ours, kJobKey);
	if (!job) {
		return;
	}

	auto get = [&](const char *key) {
		return QString::fromUtf8(obs_data_get_string(job, key));
	};

	currentJob.number = get("number");
	currentJob.client = get("client");
	currentJob.vessel = get("vessel");
	currentJob.system = get("system");
	currentJob.notes = get("notes");
	currentJob.created = get("created");
}

void writeTo(obs_data_t *modules)
{
	if (!modules) {
		return;
	}

	/* Read-modify-write. The "modules" object is shared with every plugin
	 * that stores collection data, so ours must be merged in rather than the
	 * object replaced. */
	OBSDataAutoRelease ours = obs_data_get_obj(modules, kModuleKey);
	if (!ours) {
		ours = obs_data_create();
	}

	OBSDataAutoRelease job = obs_data_create();
	auto set = [&](const char *key, const QString &value) {
		obs_data_set_string(job, key, value.toUtf8().constData());
	};

	set("number", currentJob.number);
	set("client", currentJob.client);
	set("vessel", currentJob.vessel);
	set("system", currentJob.system);
	set("notes", currentJob.notes);
	set("created", currentJob.created);

	obs_data_set_obj(ours, kJobKey, job);
	obs_data_set_obj(modules, kModuleKey, ours);
}

void onSave(obs_data_t *data, bool saving, void *)
{
	if (saving) {
		writeTo(data);
	} else {
		readFrom(data);
	}
}

} // namespace

void init()
{
	if (registered) {
		return;
	}

	/*
	 * Both callbacks receive the collection's "modules" object.
	 *
	 * The preload callback is the one that matters for reading: it runs before
	 * the scenes are built, whereas the save callback's load half fires late
	 * enough that anything reacting to a Job change would already have missed
	 * it.
	 */
	obs_frontend_add_save_callback(onSave, nullptr);
	obs_frontend_add_preload_callback(onSave, nullptr);

	registered = true;
}

const Job &current()
{
	return currentJob;
}

void setCurrent(const Job &job)
{
	currentJob = job;

	blog(LOG_INFO, "[MCJobMetadata] Job metadata set: number='%s' client='%s' vessel='%s' system='%s'",
	     job.number.toUtf8().constData(), job.client.toUtf8().constData(), job.vessel.toUtf8().constData(),
	     job.system.toUtf8().constData());

	/* Push it to disk now rather than waiting for the next autosave. A Job
	 * created and then lost to a power cut before its first save would come
	 * back anonymous, which defeats the point of recording it. */
	obs_frontend_save();
}

} // namespace MCJobMetadata
