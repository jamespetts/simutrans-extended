/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "sve_cache.h"
#include <time.h>

#include "environment.h"
#include "loadsave.h"
#include "../utils/simstring.h"
#include "../pathes.h"
#include "../sys/simsys.h"
#include "../simversion.h"

#include <sys/stat.h>


stringhashtable_tpl<sve_info_t *, N_BAGS_LARGE> sve_cache_t::cached_info;


sve_info_t::sve_info_t(const char *pak_, time_t mod_, sint32 fs, uint32 version, uint32 extended_version)
: pak(""), mod_time(mod_), file_size(fs), version(version), extended_version(extended_version)
{
	if(pak_) {
		pak = pak_;
		file_exists = true;
	}
}


bool sve_info_t::operator== (const sve_info_t &other) const
{
	return (mod_time==other.mod_time)  &&  (file_size == other.file_size)  &&  (pak.compare(other.pak)==0);
}


void sve_info_t::rdwr(loadsave_t *file)
{
	const char *s = strdup(pak.c_str());
	file->rdwr_str(s);
	if (file->is_loading()) {
		pak = s ? s : "<unknown pak>";
	}
	file->rdwr_longlong(mod_time);
	file->rdwr_long(file_size);
	if (file->get_extended_version() >= 12 )
	{
		file->rdwr_long(version);
		file->rdwr_long(extended_version);
	}
	else
	{
		if (file->is_loading())
		{
			version = extended_version = 0;
		}
	}
}


void sve_cache_t::load_cache()
{
	// load cached entries
#ifndef SPECIAL_RESCUE_12_6
	if (cached_info.empty()) {
		loadsave_t file;
		/* We rename the old cache file and remove any incomplete read version.
		 * Upon an error the cache will be rebuilt then next time.
		 */
		dr_rename( SAVE_PATH_X "_cached_exp.xml", SAVE_PATH_X "_load_cached_exp.xml" );
		if(  file.rd_open(SAVE_PATH_X "_load_cached_exp.xml")  == loadsave_t::FILE_STATUS_OK  ) {
			// ignore comment
			const char *text=NULL;
			file.rdwr_str(text);

			bool ok = true;
			while(ok) {
				xml_tag_t t(&file, "save_game_info");
				// first filename
				file.rdwr_str(text);
				if (!strempty(text)) {
					sve_info_t *svei = new sve_info_t();
					svei->rdwr(&file);
					cached_info.put(text, svei);
					text = NULL; // it is used as key, do not delete it
				}
				else {
					ok = false;
				}
			}
			if (text) {
				free(const_cast<char *>(text));
			}
			file.close();
			dr_rename( SAVE_PATH_X "_load_cached_exp.xml", SAVE_PATH_X "_cached_exp.xml" );
		}
	}
#endif
}


void sve_cache_t::write_cache()
{
	static const char *cache_file = SAVE_PATH_X "_cached.xml";

	loadsave_t file;
	if(  file.wr_open(cache_file, loadsave_t::xml, 0, "cache", SAVEGAME_VER_NR, EXTENDED_VER_NR, EXTENDED_REVISION_NR) == loadsave_t::FILE_STATUS_OK  )
	{
		const char *text="Automatically generated file. Do not edit. An invalid file may crash the game. Deleting is allowed though.";
		file.rdwr_str(text);
		for(auto const& i : cached_info) {
			// save only existing files
			if (i.value->file_exists) {
				xml_tag_t t(&file, "save_game_info");
				char const* filename = i.key;
				file.rdwr_str(filename);
				i.value->rdwr(&file);
			}
		}
		// mark end with empty entry
		{
			xml_tag_t t(&file, "save_game_info");
			text = "";
			file.rdwr_str(text);
		}
		file.close();
	}
}


const char *sve_cache_t::get_info(const char *fname)
{
	static char date[1024];

	std::string pak_extension;

	// get file information
	struct stat  sb;
	if(dr_stat(fname, &sb) != 0) {
		// file not found?
		date[0] = 0;
		return date;
	}

	// check hash table
	sve_info_t *svei = cached_info.get(fname);
	uint32 version = 0;
	uint32 extended_version = 0;
	if (svei   &&  svei->file_size == sb.st_size  &&  svei->mod_time == sb.st_mtime) {
		// compare size and mtime
		// if both are equal then most likely the files are the same
		// no need to read the file for pak_extension
		pak_extension = svei->pak.c_str();
		svei->file_exists = true;
		version = svei->version;
		extended_version = svei->extended_version;
	}
	else {
		// read pak_extension from file
		loadsave_t test;
		test.rd_open(fname); // == loadsave_t::FILE_STATUS_OK
		// add pak extension
		pak_extension = test.get_pak_extension();

		// now insert in hash_table
		sve_info_t *svei_new = new sve_info_t(pak_extension.c_str(), sb.st_mtime, sb.st_size, test.get_version_int(), test.get_extended_version());
		// copy filename
		char *key = strdup(fname);
		sve_info_t *svei_old = cached_info.set(key, svei_new);
		delete svei_old;
	}

	// write everything in string
	// add pak extension
	const size_t n = (version && extended_version) ? snprintf( date, lengthof(date), "%s (v %u.%u, e %i) - ", pak_extension.c_str(), version/1000, version%100, extended_version):
		snprintf(date, lengthof(date), "%s - ", pak_extension.c_str());

	// add the time too
	struct tm *tm = localtime(&sb.st_mtime);
	if(tm) {
		strftime(date+n, 18, "%Y-%m-%d %H:%M", tm);
	}
	else {
		tstrncpy(date, "??.??.???? ??:??", lengthof(date));
	}

	date[lengthof(date)-1] = 0;
	return date;
}


std::string sve_cache_t::get_most_recent_compatible_save()
{
	const char *best_filename = "";
	sint64 best_time = 0;

	for (stringhashtable_tpl<sve_info_t *, N_BAGS_LARGE>::iterator it = cached_info.begin(); it != cached_info.end(); ++it) {
		if (it->value->pak+"/" == env_t::objfilename && it->value->mod_time > best_time) {
			best_filename = it->key;
			best_time = it->value->mod_time;
		}
	}

	return best_filename;
}
