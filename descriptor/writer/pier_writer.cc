/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "pier_writer.h"
#include "obj_node.h"
#include "../../dataobj/ribi.h"
#include "../../dataobj/tabfile.h"
#include "get_waytype.h"
#include "imagelist_writer.h"
#include "skin_writer.h"

using std::string;


void pier_writer_t::write_obj(FILE * outfp, obj_node_t& parent, tabfileobj_t& obj)
{
	obj_node_t node(this, 76, &parent);
	write_head(outfp, node,obj);

	uint32 price					= obj.get_int("cost", 0);
	uint32 maintenance				= obj.get_int("maintenance", 1000);
	uint32 max_weight				= obj.get_int("max_weight", 250);
	uint32 pier_weight				= obj.get_int("pier_weight",0);
	sint8 max_altitude				= obj.get_int("max_altitude", 0);

	// timeline
	uint16 intro_date = obj.get_int("intro_year", DEFAULT_INTRO_DATE) * 12;
	intro_date += obj.get_int("intro_month", 1) - 1;

	uint16 retire_date = obj.get_int("retire_year", DEFAULT_RETIRE_DATE) * 12;
	retire_date += obj.get_int("retire_month", 1) - 1;

    uint8 above_way_ribi     = obj.get_int("above_way_ribi",0);
    uint8 below_way_ribi     = obj.get_int("below_way_ribi",0);

    uint32 auto_group         = obj.get_int("auto_group",0);
    uint8 auto_height        = obj.get_int("auto_height",0);

    uint32 base_mask         = obj.get_int("base_mask",0);
    uint32 middle_mask       = obj.get_int("middle_mask",0x0);
    uint32 support_mask      = obj.get_int("support_mask",0x0);

	uint32 sub_obj_mask		 = obj.get_int("sub_obj_mask",0xFFFFFFFF);
	uint32 deck_obj_mask	 = obj.get_int("deck_obj_mask",0x0000FFFF);

	uint8 above_slope		 = obj.get_int("above_slope",0);
	uint8 drag_ribi			   = obj.get_int("drag_ribi",0);
	uint8 above_way_supplement = obj.get_int("above_way_supplement",0);

	uint32 base_mask_2		   = obj.get_int("base_mask_2",0);
	uint32 middle_mask_2	   = obj.get_int("middle_mask_2",0);
	uint32 support_mask_2	   = obj.get_int("support_mask_2",0);

	//place flags in extra bits of drag_ribi
	drag_ribi |= obj.get_int("keep_dry",0) ? 0x10 : 0;
	drag_ribi |= obj.get_int("bottom_only",0) ? 0x20 : 0;
	drag_ribi |= obj.get_int("bottom_only",0) ? 0x20 : 0;
	drag_ribi |= obj.get_int("above_way_supplement",0) ? 0x40 : 0;
	drag_ribi |= obj.get_int("low_waydeck",0) ? 0x80 : 0;

	uint16 max_speed			= obj.get_int("topspeed",0xFFFF);
	uint16 max_axle_load		= obj.get_int("max_weight",0xFFFF);

	uint8 tooltipflagsauto		= obj.get_int("tooltipflagsauto",0);
	uint8 tooltipflagsmanual	= obj.get_int("tooltipflagsmanual",0);

	above_way_ribi |= tooltipflagsauto<<4;
	below_way_ribi |= tooltipflagsmanual<<4;

	uint16 version = 0x8009;

	version |= EX_VER;

	version += 0x300; //Version number of node times 0x100

	node.write_uint16(outfp, version,					0);
	node.write_uint32(outfp, price,						2);
	node.write_uint32(outfp, maintenance,				6);
	node.write_uint16(outfp, intro_date,				10);
	node.write_uint16(outfp, retire_date,				12);
	node.write_uint32(outfp, max_weight,				14);
	node.write_sint8(outfp, max_altitude,				18);
	node.write_uint8(outfp, above_way_ribi,				19);
	node.write_uint8(outfp, below_way_ribi,				20);
	node.write_uint32(outfp, base_mask,					21);
	node.write_uint32(outfp, support_mask,				25);
	node.write_uint32(outfp, sub_obj_mask,				29);
	node.write_uint32(outfp, deck_obj_mask,				33);
	node.write_uint8(outfp,	0,							37); //old auto_group
	node.write_uint8(outfp, auto_height,				38);

	uint8 seasons=2;
	string str = obj.get("backimage[00][0][1]");
	if(str.empty()) seasons=1;

	node.write_uint8(outfp, seasons,                    39);
	node.write_uint8(outfp,above_slope,					40);

	uint8 rot_sym=4;
	str = obj.get("backimage[00][3][0]");
	if(str.empty()) rot_sym=2;
	str = obj.get("backimage[00][1][0]");
	if(str.empty()) rot_sym=1;

	node.write_uint8(outfp, rot_sym,					41);

	node.write_uint32(outfp, middle_mask,               42);
	node.write_uint32(outfp, pier_weight,				46);
	node.write_uint8(outfp, drag_ribi,					50);
	node.write_uint8(outfp, above_way_supplement,		51);

	node.write_uint32(outfp, support_mask_2, 52);
	node.write_uint32(outfp, middle_mask_2,  56);
	node.write_uint32(outfp, base_mask_2,    60);

	node.write_uint16(outfp, max_speed,      64);
	node.write_uint16(outfp, max_axle_load,  66);

	node.write_uint32(outfp, auto_group,     68);
	node.write_uint32(outfp, 0,			     72); //extra for future use

	slist_tpl<string> backkeys;
	slist_tpl<string> frontkeys;

	for(int k = 0; k < seasons; k++){ //each season
		for(int j = 0; j < rot_sym; j++){ //each rotation of pier
			for(int i = 0; i < 81; i++){ //each slope of ground

				char keybuf[32];
				string value;

				sprintf(keybuf, "backimage[%02d][%d][%d]",i,j,k);
				value = obj.get(keybuf);
				backkeys.append(value);

				sprintf(keybuf, "frontimage[%02d][%d][%d]",i,j,k);
				value = obj.get(keybuf);
				frontkeys.append(value);

			}
		}
		imagelist_writer_t::instance()->write_obj(outfp, node, backkeys);
		imagelist_writer_t::instance()->write_obj(outfp, node, frontkeys);

		if(k==0){
			slist_tpl<string> cursorkeys;
			cursorkeys.append( string( obj.get("cursor") ) );
			cursorkeys.append( string( obj.get("icon") ) );

			cursorskin_writer_t::instance()->write_obj( outfp, node, obj, cursorkeys );

			cursorkeys.clear();
		}

		backkeys.clear();
		frontkeys.clear();
	}

	node.write(outfp);
}
