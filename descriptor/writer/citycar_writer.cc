/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string>
#include "../../dataobj/tabfile.h"
#include "../../simdebug.h"
#include "../../tpl/vector_tpl.h"
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

	// The passenger wealth classes for which this car is available, as a
	// comma-separated list of integer class indices (0 is the lowest class).
	// If the key is absent, the car is available to every class.
	vector_tpl<uint8> available_classes(2);
	{
		const char *const class_list = obj.get_string("class", NULL);
		if (class_list != NULL && *class_list != 0)
		{
			int *const classes = obj.get_ints("class");
			const uint32 count = classes[0];
			if (count > 255)
			{
				dbg->fatal(obj_writer_t::last_name, "class= lists %u classes; at most 255 are allowed", count);
			}
			for (uint32 c = 0; c < count; c++)
			{
				if (classes[c + 1] < 0 || classes[c + 1] > 255)
				{
					dbg->fatal(obj_writer_t::last_name, "class index %i is not a valid passenger class", classes[c + 1]);
				}
				available_classes.append((uint8)classes[c + 1]);
			}
			delete [] classes;
		}
		else if (class_list != NULL)
		{
			dbg->fatal(obj_writer_t::last_name, "class= is empty; omit the key to make the car available to all classes");
		}
	}

	const uint32 class_count = available_classes.get_count();

	// Version 5 fields: the passenger capacity of one car and the vehicle
	// length in carunits. Omitted keys give zero, which keeps the historic
	// traffic-level car fill.
	const int capacity = obj.get_int("capacity", 0);
	const int car_length = obj.get_int("length", 0);
	if (capacity < 0 || capacity > 255)
	{
		dbg->fatal(obj_writer_t::last_name, "capacity %i is not a valid passenger capacity", capacity);
	}
	if (car_length < 0 || car_length > 255)
	{
		dbg->fatal(obj_writer_t::last_name, "length %i is not a valid vehicle length in carunits", car_length);
	}

	obj_node_t node(this, 14 + 1 + class_count + 2, &parent);

	uint16 const dist_weight = obj.get_int("distributionweight", 1);

	uint16 const intro_date =
		obj.get_int("intro_year", DEFAULT_INTRO_DATE) * 12 +
		obj.get_int("intro_month", 1) - 1;

	uint16 const retire_date =
		obj.get_int("retire_year", DEFAULT_RETIRE_DATE) * 12 +
		obj.get_int("retire_month", 1) - 1;

	uint16 const topspeed = obj.get_int("speed", 80) * 16;

	// new version with intro, obsolete dates, time-based comfort, the
	// passenger classes for which the car is available, and the car's
	// capacity and length
	node.write_uint16(fp, 0x8005,        0); // version information
	node.write_uint16(fp, dist_weight,    2);
	node.write_uint16(fp, topspeed,        4);
	node.write_uint16(fp, intro_date,    6);
	node.write_uint16(fp, retire_date, 8);
	node.write_uint16(fp, read_citycar_time(obj, "maximum_comfortable_journey"), 10);
	node.write_uint16(fp, read_citycar_time(obj, "maximum_tolerable_journey"), 12);
	node.write_uint8(fp, (uint8)class_count, 14);
	for (uint32 c = 0; c < class_count; c++)
	{
		node.write_uint8(fp, available_classes[c], 15 + c);
	}
	node.write_uint8(fp, (uint8)capacity, 15 + class_count);
	node.write_uint8(fp, (uint8)car_length, 16 + class_count);

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
