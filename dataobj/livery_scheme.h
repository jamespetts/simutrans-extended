/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_LIVERY_SCHEME_H
#define DATAOBJ_LIVERY_SCHEME_H


#include <string>

#include "../simtypes.h"
#include "../tpl/vector_tpl.h"

class loadsave_t;
class vehicle_desc_t;

struct livery_t
{
	std::string name;
	uint16 intro_date;

	bool operator == (const livery_t comparator) const { return comparator.intro_date == intro_date && name == comparator.name; }
	bool operator != (const livery_t comparator) const { return comparator.intro_date != intro_date || name != comparator.name; }
};

class livery_scheme_t
{
private:
	std::string scheme_name;
	vector_tpl<livery_t> liveries;
	uint16 retire_date;

public:
	livery_scheme_t(const char* n, const uint16 date);

	const char* get_name() const { return scheme_name.c_str(); }

	void add_livery(const char* name, uint16 intro)
	{
		livery_t liv = {name, intro};
		liveries.append(liv);
	}

	bool is_available(uint16 date) const
	{
		if(date == 0)
		{
			// Allow all schemes when the timeline is turned off.
			return true;
		}

		if(date > retire_date)
		{
			return false;
		}
		else
		{
			for(uint32 i = 0; i < liveries.get_count(); i ++)
			{
				if(date >= liveries[i].intro_date)
				{
					return true;
				}
			}
			return false;
		}
	}

	const char* get_latest_available_livery(uint16 date, const vehicle_desc_t* desc) const;

	// returns whether the livery belongs to this livery scheme
	// If you want to check if it already appears, pass get_timeline_year_month @ Ranran
	bool is_contained(const char* name, uint16 date = 0) const
	{
		for(auto livery : liveries)
		{
			if ((date && date >= livery.intro_date && livery.name.compare(name) == 0)
				|| (!date && livery.name.compare(name) == 0))
			{
				return true;
			}
		}
		return false;
	}

	void rdwr(loadsave_t *file);

	bool operator == (const livery_scheme_t comparator) const;
};
#endif
