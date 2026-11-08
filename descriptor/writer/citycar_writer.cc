/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string>
#include "../../dataobj/tabfile.h"
#include "../../simdebug.h"
#include "obj_node.h"
#include "text_writer.h"
#include "imagelist_writer.h"
#include "citycar_writer.h"


static uint16 read_citycar_time(tabfileobj_t &obj, const char *base_name)
{
	char key[128];

	sprintf(key, "%s_minutes", base_name);
	const char *minutes = obj.get(key);
	sprintf(key, "%s_tenths", base_name);
	const char *tenths = obj.get(key);

	const bool has_minutes = minutes != NULL && *minutes != 0;
	const bool has_tenths = tenths != NULL && *tenths != 0;
	if (has_minutes && has_tenths)
	{
		dbg->fatal(obj_writer_t::last_name, "%s: specify only one of the _minutes and _tenths forms", base_name);
	}
	if (has_tenths)
	{
		return (uint16)atoi(tenths);
	}
	if (has_minutes)
	{
		return (uint16)(atoi(minutes) * 10);
	}
	return 0;
}


void citycar_writer_t::write_obj(FILE* fp, obj_node_t& parent, tabfileobj_t& obj)
{
	int i;

	obj_node_t node(this, 14, &parent);

	uint16 const dist_weight = obj.get_int("distributionweight", 1);

	uint16 const intro_date =
		obj.get_int("intro_year", DEFAULT_INTRO_DATE) * 12 +
		obj.get_int("intro_month", 1) - 1;

	uint16 const retire_date =
		obj.get_int("retire_year", DEFAULT_RETIRE_DATE) * 12 +
		obj.get_int("retire_month", 1) - 1;

	uint16 const topspeed = obj.get_int("speed", 80) * 16;

	// new version with intro, obsolete dates and time-based comfort
	node.write_uint16(fp, 0x8003,        0); // version information
	node.write_uint16(fp, dist_weight,    2);
	node.write_uint16(fp, topspeed,        4);
	node.write_uint16(fp, intro_date,    6);
	node.write_uint16(fp, retire_date, 8);
	node.write_uint16(fp, read_citycar_time(obj, "maximum_comfortable_journey"), 10);
	node.write_uint16(fp, read_citycar_time(obj, "maximum_tolerable_journey"), 12);

	write_head(fp, node, obj);

	static const char* const dir_codes[] = {
		"s", "w", "sw", "se", "n", "e", "ne", "nw"
	};
	slist_tpl<std::string> keys;
	std::string str;

	for (i = 0; i < 8; i++) {
		char buf[40];

		sprintf(buf, "image[%s]", dir_codes[i]);
		str = obj.get(buf);
		keys.append(str);
	}
	imagelist_writer_t::instance()->write_obj(fp, node, keys);

	node.write(fp);
}
