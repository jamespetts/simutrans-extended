/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <cmath>
#include <string>
#include "../../dataobj/tabfile.h"
#include "obj_node.h"
#include "obj_pak_exception.h"
#include "../way_obj_desc.h"
#include "text_writer.h"
#include "imagelist_writer.h"
#include "skin_writer.h"
#include "get_waytype.h"
#include "way_obj_writer.h"

using std::string;

/**
 * Write a way object (lamps, overheadwires)
 */
void way_obj_writer_t::write_obj(FILE* outfp, obj_node_t& parent, tabfileobj_t& obj)
{
	static const char* const ribi_codes[26] = {
		"-", "n",  "e",  "ne",  "s",  "ns",  "se",  "nse",
		"w", "nw", "ew", "new", "sw", "nsw", "sew", "nsew",
		"nse1", "new1", "nsw1", "sew1", "nsew1", // different crossings: northwest/southeast is oneway
		"nse2", "new2", "nsw2", "sew2", "nsew2",
	};
	int ribi, slope;

	obj_node_t node(this, 23, &parent);


	// Version needs high bit set as trigger -> this is required
	// as marker because formerly nodes were unversionend
	uint16 version     = 0x8001;

	// This is the overlay flag for Simutrans-Extended
	// This sets the *second* highest bit to 1.
	version |= EX_VER;

	// Finally, this is the extended version number. This is *added*
	// to the standard version number, to be subtracted again when read.
	// Start at 0x100 and increment in hundreds (hex).
	version += 0x200;

	uint32 price       = obj.get_int("cost",        100);
	uint32 maintenance = obj.get_int("maintenance", 100);
	sint32 topspeed    = obj.get_int("topspeed",    999);

	uint16 intro_date = obj.get_int("intro_year", DEFAULT_INTRO_DATE) * 12;
	intro_date +=        obj.get_int("intro_month", 1) - 1;

	uint16 retire  = obj.get_int("retire_year", DEFAULT_RETIRE_DATE) * 12;
	retire +=        obj.get_int("retire_month", 1) - 1;

	uint8 wtyp     =  get_waytype(obj.get("waytype"));
	uint8 own_wtyp =  get_waytype(obj.get("own_waytype"));

	uint8 is_tall   =  obj.get_int("is_tall", 0) ? 1 : 0;

	// Way constraints
	// One byte for permissive, one byte for prohibitive.
	// Therefore, 8 possible constraints of each type.
	// Permissive: way allows vehicles with matching constraint:
	// vehicles not allowed on any other sort of way. Vehicles
	// without that constraint also allowed on the way.
	// Prohibitive: way allows only vehicles with matching constraint:
	// vehicles with matching constraint allowed on other sorts of way.
	// @author: jamespetts

	uint8 permissive_way_constraints = 0;
	uint8 prohibitive_way_constraints = 0;
	char buf_permissive[60];
	char buf_prohibitive[60];
	//Read the values from a file, and put them into an array.
	for(uint8 i = 0; i < 8; i++)
	{
		sprintf(buf_permissive, "way_constraint_permissive[%d]", i);
		sprintf(buf_prohibitive, "way_constraint_prohibitive[%d]", i);
		uint8 tmp_permissive = (obj.get_int(buf_permissive, 255));
		uint8 tmp_prohibitive = (obj.get_int(buf_prohibitive, 255));

		//Compress values into a single byte using bitwise OR.
		if(tmp_permissive < 8)
		{
			permissive_way_constraints = (tmp_permissive > 0) ? permissive_way_constraints | (uint8)pow(2, (double)tmp_permissive) : permissive_way_constraints | 1;
		}
		if(tmp_prohibitive < 8)
		{
			prohibitive_way_constraints = (tmp_prohibitive > 0) ? prohibitive_way_constraints | (uint8)pow(2, (double)tmp_prohibitive) : prohibitive_way_constraints | 1;
		}
	}

	node.write_uint16(outfp, version,					0);
	node.write_uint32(outfp, price,						2);
	node.write_uint32(outfp, maintenance,				6);
	node.write_sint32(outfp, topspeed,					10);
	node.write_uint16(outfp, intro_date,				14);
	node.write_uint16(outfp, retire,					16);
	node.write_uint8 (outfp, wtyp,						18);
	node.write_uint8 (outfp, own_wtyp,					19);
	node.write_uint8(outfp, permissive_way_constraints,	20);
	node.write_uint8(outfp, prohibitive_way_constraints,21);
	node.write_uint8(outfp, is_tall,                    22);

	write_head(outfp, node, obj);

	slist_tpl<string> front_list;
	slist_tpl<string> back_list;

	for (ribi = 0; ribi < lengthof(ribi_codes); ribi++) {
		char buf[40];
		sprintf(buf, "frontimage[%s]", ribi_codes[ribi]);
		string str = obj.get(buf);
		front_list.append(str);
		sprintf(buf, "backimage[%s]", ribi_codes[ribi]);
		string str2 = obj.get(buf);
		back_list.append(str2);
	}
	imagelist_writer_t::instance()->write_obj(outfp, node, front_list);
	imagelist_writer_t::instance()->write_obj(outfp, node, back_list);

	front_list.clear();
	back_list.clear();

	for(  slope = 3;  slope <= 12;  slope += 3  ) {
		char buf[40];
		sprintf( buf, "frontimageup[%d]", slope );
		string str = obj.get(buf);
		front_list.append(str);
		sprintf( buf, "backimageup[%d]", slope );
		string str2 = obj.get(buf);
		back_list.append(str2);
	}
	for(  slope = 3;  slope <= 12;  slope += 3  ) {
		char buf[40];
		sprintf( buf, "frontimageup2[%d]", slope );
		string str = obj.get(buf);
		if(  !str.empty()  ) {
			front_list.append(str);
		}
		sprintf( buf, "backimageup2[%d]", slope );
		string str2 = obj.get(buf);
		if(  !str2.empty()  ) {
			back_list.append(str2);
		}
	}
	imagelist_writer_t::instance()->write_obj(outfp, node, front_list);
	imagelist_writer_t::instance()->write_obj(outfp, node, back_list);

	front_list.clear();
	back_list.clear();

	for (ribi = 3; ribi <= 12; ribi += 3) {
		char buf[40];
		sprintf(buf, "frontdiagonal[%s]", ribi_codes[ribi]);
		string str = obj.get(buf);
		front_list.append(str);
		sprintf(buf, "backdiagonal[%s]", ribi_codes[ribi]);
		string str2 = obj.get(buf);
		back_list.append(str2);
	}
	imagelist_writer_t::instance()->write_obj(outfp, node, front_list);
	imagelist_writer_t::instance()->write_obj(outfp, node, back_list);

	slist_tpl<string> cursorkeys;
	cursorkeys.append(string(obj.get("cursor")));
	cursorkeys.append(string(obj.get("icon")));
	cursorskin_writer_t::instance()->write_obj(outfp, node, obj, cursorkeys);

	// node.write_data(fp, &desc);
	node.write(outfp);
}
