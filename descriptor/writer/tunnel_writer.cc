/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string>
#include <cmath>
#include "../../dataobj/tabfile.h"
#include "../../dataobj/ribi.h"
#include "../tunnel_desc.h"
#include "obj_node.h"
#include "text_writer.h"
#include "xref_writer.h"
#include "imagelist_writer.h"
#include "skin_writer.h"
#include "get_waytype.h"
#include "tunnel_writer.h"

using std::string;

void tunnel_writer_t::write_obj(FILE* fp, obj_node_t& parent, tabfileobj_t& obj)
{
	static const char* const ribi_codes[26] = {
		"-", "n",  "e",  "ne",  "s",  "ns",  "se",  "nse",
		"w", "nw", "ew", "new", "sw", "nsw", "sew", "nsew",
		"nse1", "new1", "nsw1", "sew1", "nsew1",	// different crossings: northwest/southeast is straight
		"nse2", "new2", "nsw2", "sew2", "nsew2",
	};
	int ribi, hang;

	uint32 node_size=33;

	sint32 topspeed					= obj.get_int("topspeed",    1000);
	sint32 topspeed_gradient_1		= obj.get_int("topspeed_gradient_1",    topspeed);
	sint32 topspeed_gradient_2		= obj.get_int("topspeed_gradient_2",    topspeed_gradient_1);
	uint32 price					= obj.get_int("cost",          0);
	uint32 maintenance				= obj.get_int("maintenance",1000);
	uint8 waytype_t					= get_waytype(obj.get("waytype"));
	uint16 axle_load				= obj.get_int("axle_load",   9999);
	sint8 max_altitude				= obj.get_int("max_altitude", 0);
	uint8 max_vehicles_on_tile		= obj.get_int("max_vehicles_on_tile", 251);

	uint8 flags = 0;

	flags |= obj.get_int("half_height",0) ? 0x01 : 0;

	uint32 subsea_cost;
	uint32 subsea_maintenance;
	if(((subsea_cost=obj.get_int("subsea_cost",0xFFFFFFFF))!=0xFFFFFFFF) || obj.get_int("allow_subsea",0)){
		flags|=0x02;
		if(subsea_cost==0xFFFFFFFF){
			subsea_cost=0;
		}
		subsea_maintenance = obj.get_int("subsea_maintenance",0);
		node_size+=8;
	}

	uint32 subbuilding_cost;
	if(obj.get_int("forbid_subbuilding",0)){
		flags|=0x04;
		subbuilding_cost=0xFFFFFFFF;
		node_size+=4;
	}else if((subbuilding_cost=obj.get_int("subbuilding_cost",0))!=0){
		flags|=0x04;
		node_size+=4;
	}

	uint32 subwaterline_cost=0;
	uint32 subwaterline_maintenance=0;
	if(((subwaterline_cost=obj.get_int("subwaterline_cost",0xFFFFFFFF))!=0xFFFFFFFF) || obj.get_int("allow_subsea",0)){
		flags|=0x08;
		subwaterline_maintenance = obj.get_int("subwaterline_maintenance",0);
		if(subwaterline_cost==0xFFFFFFFF){
			subwaterline_cost=0;
		}
		node_size+=8;
	}

	uint32 depth_cost=obj.get_int("depth_cost",0);
	uint32 depth2_cost=obj.get_int("depth_squared_cost",0);
	uint32 subway_cost=obj.get_int("sub_way_cost",0);
	uint8 depth_limit;
	depth_limit=obj.get_int("depth_limit",0) & 0x7F;
	uint8 underwater_limit=obj.get_int("sea_depth_limit",0);
	if(underwater_limit){
		flags|=0x20;
		node_size+=1;
	}
	if(depth_cost || depth2_cost || subway_cost || depth_limit){
		flags|=0x10;
		node_size+=13;
	}
	uint16 length_limit=obj.get_int("length_limit",0);
	if(length_limit){
		flags|=0x40;
		node_size+=2;
		dbg->warning("tunnel_writer::write_node()","High values of length_limit can reduce performance of tunnel construction");
	}

	// BG, 11.02.2014: max_weight was missused as axle_load
	// in experimetal before standard introduced axle_load.
	//
	// Therefore set new standard axle_load with old extended
	// max_weight, if axle_load not specified, but max_weight.
	if (axle_load == 9999)
	{
		uint32 max_weight  = obj.get_int("max_weight",   999);
		if (max_weight != 999)
			axle_load = max_weight;
	}

	// timeline
	uint16 intro_date  = obj.get_int("intro_year", DEFAULT_INTRO_DATE) * 12;
	intro_date += obj.get_int("intro_month", 1) - 1;

	uint16 retire_date  = obj.get_int("retire_year", DEFAULT_RETIRE_DATE) * 12;
	retire_date += obj.get_int("retire_month", 1) - 1;

	// predefined string for directions
	static const char* const indices[] = { "n", "s", "e", "w" };
	static const char* const add[] = { "", "l", "r", "m" };
	char buf[40];

	// Check for seasons
	sint8 number_of_seasons = 0;
	sprintf(buf, "%simage[%s][1]", "front", indices[0]);
	string str = obj.get(buf);
	if(!str.empty()) {
		// Snow images are present.
		number_of_seasons = 1;
	}

	// Check for broad portals
	uint8 number_portals = 1;
	sprintf(buf, "%simage[%s%s][0]", "front", indices[0], add[1]);
	str = obj.get(buf);
	if (str.empty()) {
		// Test short version
		sprintf(buf, "%simage[%s%s]", "front", indices[0], add[1]);
		str = obj.get(buf);
	}
	if(!str.empty()) {
		number_portals = 4;
	}

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

	obj_node_t node(this, node_size, &parent);

	// Version uses always high bit set as trigger
	// version 2: snow images
	// Version 3: pre-defined ways
	// version 4: snow images + underground way image + broad portals
	uint16 version = 0x8005;

	// This is the overlay flag for Simutrans-Extended
	// This sets the *second* highest bit to 1.
	version |= EX_VER;

	// Finally, this is the extended version number. This is *added*
	// to the standard version number, to be subtracted again when read.
	// Start at 0x100 and increment in hundreds (hex).
	version += 0x300;

	node.write_uint16(fp, version,						0);
	node.write_sint32(fp, topspeed,						2);
	node.write_uint32(fp, price,						6);
	node.write_uint32(fp, maintenance,					10);
	node.write_uint8 (fp, waytype_t,						14);
	node.write_uint16(fp, intro_date,					15);
	node.write_uint16(fp, retire_date,				17);
	node.write_uint16(fp, axle_load,					19);
	node.write_sint8(fp, number_of_seasons,				21);
	// has was (uint8) is here but filled later
	node.write_sint8(fp, (number_portals==4),			23);
	// extended 1 additions:
	node.write_uint8(fp, permissive_way_constraints,	24);
	node.write_uint8(fp, prohibitive_way_constraints,	25);
	node.write_uint16(fp, topspeed_gradient_1,			26);
	node.write_uint16(fp, topspeed_gradient_2,			28);
	node.write_sint8(fp, max_altitude,					30);
	node.write_uint8(fp, max_vehicles_on_tile,			31);
	node.write_uint8(fp, flags,							32);

	int offset = 32+1;

	if(flags & 0x02){
		node.write_uint32(fp, subsea_cost,				offset);
		node.write_uint32(fp, subsea_maintenance,		offset+4);
		offset+=8;
	}

	if(flags & 0x04){
		node.write_uint32(fp, subbuilding_cost,			offset);
		offset+=4;
	}

	if(flags & 0x08){
		node.write_uint32(fp, subwaterline_cost,		offset);
		node.write_uint32(fp, subwaterline_maintenance,	offset+4);
		offset+=8;
	}

	if(flags & 0x10){
		node.write_uint32(fp, subway_cost,				offset);
		node.write_uint32(fp, depth_cost,				offset+4);
		node.write_uint32(fp, depth2_cost,				offset+8);
		node.write_uint8(fp,  depth_limit,				offset+12);
		offset+=13;
	}

	if(flags & 0x20){
		node.write_uint8(fp,  underwater_limit,         offset);
		offset+=1;
	}

	if(flags & 0x40){
		node.write_uint16(fp,  length_limit,             offset);
		offset+=2;
	}

	write_head(fp, node, obj);

	// and now the images
	slist_tpl<string> backkeys;
	slist_tpl<string> frontkeys;

	slist_tpl<string> cursorkeys;
	cursorkeys.append(string(obj.get("cursor")));
	cursorkeys.append(string(obj.get("icon")));

	// These are the portal images only.
	for(  uint8 season = 0;  season <= number_of_seasons;  season++  ) {
		for(  uint8 pos = 0;  pos < 2;  pos++  ) {
			for(  uint8 j = 0;  j < number_portals;  j++  ) {
				for(  uint8 i = 0;  i < 4;  i++  ) {
					sprintf(buf, "%simage[%s%s][%d]", pos ? "back" : "front", indices[i], add[j], season);
					string str = obj.get(buf);
					if (str.empty() && season == 0) {
						// Test also the short version.
						sprintf(buf, "%simage[%s%s]", pos ? "back" : "front", indices[i], add[j]);
						str = obj.get(buf);
					}
					(pos ? &backkeys : &frontkeys)->append(str);
				}
			}
		}
		imagelist_writer_t::instance()->write_obj(fp, node, backkeys);
		imagelist_writer_t::instance()->write_obj(fp, node, frontkeys);
		backkeys.clear();
		frontkeys.clear();
		if(season == 0) {
			cursorskin_writer_t::instance()->write_obj(fp, node, obj, cursorkeys);
		}
	}

	uint32 has_tunnel_internal_images = 0;

	// These are the internal images
	// Code adapted from the way writer
	slist_tpl<string> keys;
	static const char* const image_type[] = { "", "front" };
	for (int backtofront = 0; backtofront<2; backtofront++)
	{
		for (ribi = 0; ribi < 16; ribi++)
		{
			char buf[64];

			sprintf(buf, "%sundergroundimage[%s]", image_type[backtofront], ribi_codes[ribi]);
			string str = obj.get(buf);
			keys.append(str);
			has_tunnel_internal_images ++;
		}
		imagelist_writer_t::instance()->write_obj(fp, node, keys);

		keys.clear();
		for (hang = 3; hang <= 12; hang += 3)
		{
			char buf[64];

			sprintf(buf, "%sundergroundimageup[%d]", image_type[backtofront], hang);
			string str = obj.get(buf);
			keys.append(str);
			has_tunnel_internal_images++;
		}
		for (hang = 3; hang <= 12; hang += 3)
		{
			char buf[64];

			sprintf(buf, "%sundergroundimageup2[%d]", image_type[backtofront], hang);
			string str = obj.get(buf);
			if (!str.empty())
			{
				keys.append(str);
				has_tunnel_internal_images++;
			}
		}
		imagelist_writer_t::instance()->write_obj(fp, node, keys);

		keys.clear();
		for (ribi = 3; ribi <= 12; ribi += 3)
		{
			char buf[64];

			sprintf(buf, "%sundergrounddiagonal[%s]", image_type[backtofront], ribi_codes[ribi]);
			string str = obj.get(buf);
			keys.append(str);
			has_tunnel_internal_images++;
		}
		imagelist_writer_t::instance()->write_obj(fp, node, keys);
		keys.clear();
	}

	str = obj.get("way");
	if (!str.empty())
	{
		xref_writer_t::instance()->write_obj(fp, node, obj_way, str.c_str(), false);
		if (has_tunnel_internal_images >= 4)
		{
			node.write_sint8(fp, 3, 22);
		}
		else
		{
			node.write_sint8(fp, 1, 22);
		}
	}
	else
	{
		if (has_tunnel_internal_images >= 4)
		{
			node.write_sint8(fp, 2, 22);
		}
		else
		{
			node.write_sint8(fp, 0, 22);
		}
	}

	cursorkeys.clear();

	node.write(fp);
}
