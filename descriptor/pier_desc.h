/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DESCRIPTOR_PIER_DESC_H
#define DESCRIPTOR_PIER_DESC_H


#include "../utils/cbuffer_t.h"
#include "way_desc.h"
#include "../dataobj/ribi.h"
#include "../dataobj/way_constraints.h"

class grund_t;

//pier and (sometimes) deck for viaduct
class pier_desc_t : public obj_desc_transport_infrastructure_t {
    friend class pier_reader_t;

private:
    ribi_t::ribi above_way_ribi :4;      //which directions ways can go on deck (0 if no deck)
    ribi_t::ribi below_way_ribi :4;      //which directions ways can go beneath (0 for no ways)


    slope_t::type above_slope;        //slope of ramp

    uint32 auto_group;                 //group for automatic placement (0 is no group)
    uint8 auto_height;                //bit feild for automatic placement height
                                      //MSB to LSB: avoid placement; (remain bits future use...) Reserved; Only 1 pier; Top Peir; Upper third; Middle; lower Third; Bottom;

    //masks are rotated by 8 bits during rotation;
    uint64 base_mask;           //bit field for where foundation is needed (up to pakset designer)
    uint64 middle_mask;         //bit field to prevend piers occupying the same physical space (up to pakset designer)
    uint64 support_mask;        //bit field for locations on deck (or lack thereof) where columns can be supported (up to packset designer)
    uint32 sub_obj_mask;        //restrict buildings coninsiding within
    uint32 deck_obj_mask;       //restrict ways and buildings on deck

    //other items
    uint32 max_weight; //(future use)
    uint32 pier_weight; //(future use)
    uint8 number_of_seasons : 2;
    uint8 rotational_symmetry : 3;

    uint8 drag_ribi : 4; //direction for dragging
    uint8 above_way_supplement : 1; //only allow above_way_ribi if not only pier
    uint8 bottom_only : 1; //restrict pier to nature tiles
    uint8 keep_dry : 1; //restict pier to dry land
    uint8 low_waydeck : 1; //pier supports ways in pier, not on top

    uint8 tooltipflags_a : 2; //pakset flags for tooltips (auto tools)
    uint8 tooltipflags_m : 2; //pakset flags for tooltips (manual tools)

    static uint32 rotate_mask(uint32 m,uint8 r){
        return (m << (8*r)) | (m >> (32 - 8*r));
    }

    static uint64 rotate_mask(uint64 m,uint8 r){
        return (m << (16*r)) | (m >> (64 - 16*r));
    }

    //TODO maybe move this into ribi.h
    static ribi_t::ribi rotate_ribi(ribi_t::ribi x, uint8 r){
        return ((x | x<<4) >> (r) ) & 0xf;
    }

    uint8 img_rotation(uint8 rot) const {
        switch (rotational_symmetry) {
        case 4: return rot;
        case 2: return rot&1;
        case 1: return 0;
        }
        return 0;
    }

    tool_t *auto_builder;
    tool_t *alt_tools[3];
    cbuffer_t alt_tool_params[3];
public:
	const char *get_name() const { return get_cursor()->get_name(); }
	const char *get_copyright() const { return get_cursor()->get_copyright(); }

	skin_desc_t const* get_cursor() const { return get_child<skin_desc_t>(4); }

	//accessors
	ribi_t::ribi get_above_way_ribi(uint8 rotation=0) const {return rotate_ribi(above_way_ribi,rotation);}
	ribi_t::ribi get_below_way_ribi(uint8 rotation=0) const {return rotate_ribi(below_way_ribi, rotation);}
	slope_t::type get_above_slope(uint8 rotation=0) const;
	uint32 get_auto_group() const {return auto_group;}
	uint8 get_auto_height() const {return auto_height;}
	bool get_auto_height_avoid() const {return auto_height & 0x80;}

	uint64 get_base_mask(uint8 rotation=0) const {return rotate_mask(base_mask,rotation);}
	uint64 get_middle_mask(uint8 rotation=0) const {return rotate_mask(middle_mask,rotation);}
	uint64 get_support_mask(uint8 rotation=0) const {return rotate_mask(support_mask, rotation);}
	uint32 get_sub_obj_mask() const {return sub_obj_mask;}
	uint32 get_deck_obj_mask() const {return deck_obj_mask;}
	bool get_keep_dry() const {return keep_dry;}
	bool get_bottom_only() const {return bottom_only;};
	bool get_above_way_supplement() const {return above_way_supplement;}
	bool get_low_waydeck() const {return low_waydeck;}

	ribi_t::ribi get_drag_ribi(uint8 rotation=0) const {return rotate_ribi(drag_ribi, rotation);}

	uint16 get_max_axle_load() const {return axle_load;}

	bool get_tooltip_flag_a(uint8 flag) const { return tooltipflags_a & ((uint8)1 << flag);}
	bool get_tooltip_flag_m(uint8 flag) const { return tooltipflags_m & ((uint8)1 << flag);}

	image_id get_background(slope_t::type slope, uint8 rotation, uint8 season) const;

	image_id get_true_background(slope_t::type slope, uint8 rotataion, uint8 season) const {
		if(rotataion>=rotational_symmetry) return IMG_EMPTY;
		return get_background(slope,rotataion,season);
	}

	image_id get_foreground(slope_t::type slope, uint8 rotation, uint8 season) const;

	void calc_checksum(checksum_t *chk) const;

	tool_t* get_auto_builder() const {
		return auto_builder;
	}

	void set_auto_builder( tool_t *tool){
		auto_builder=tool;
	}

	tool_t* get_alt_tool(int i) const {
		return alt_tools[i];
	}

	void set_alt_tool(int i, tool_t *tool){
		alt_tools[i]=tool;
	}

	cbuffer_t& ref_tool_string(int i){return alt_tool_params[i];}


	static const slope_t::type low_waydeck_image;
	static const slope_t::type auto_tool_cursor_image;
	static const slope_t::type auto_tool_icon_image;
	static const slope_t::type rotation_select_image;
	static const slope_t::type alt_tool_icon;
	static const slope_t::type parapet[];

};

#endif // PIER_DESC_H
