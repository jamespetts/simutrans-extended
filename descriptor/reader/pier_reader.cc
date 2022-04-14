/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "pier_reader.h"
#include "../../simtypes.h"
#include "../pier_desc.h"
#include "../../bauer/pier_builder.h"
#include "../../network/pakset_info.h"

void pier_reader_t::register_obj(obj_desc_t *&data){
    pier_desc_t *desc = static_cast<pier_desc_t *>(data);
    pier_builder_t::register_desc(desc);

    checksum_t *chk = new checksum_t();
    desc->calc_checksum(chk);
    pakset_info_t::append(desc->get_name(), get_type(),chk);
}

void pier_reader_t::shufflemask(uint64 &mask){
    uint64 tmp=mask;
    mask&=        0xFF000000000000FF;
    mask|= (tmp & 0x000000000000FF00) << 8;
    mask|= (tmp & 0x0000000000FF0000) << 16;
    mask|= (tmp & 0x00000000FF000000) << 24;
    mask|= (tmp & 0x000000FF00000000) >> 24;
    mask|= (tmp & 0x0000FF0000000000) >> 16;
    mask|= (tmp & 0x00FF000000000000) >> 8;
}

obj_desc_t * pier_reader_t::read_node(FILE *fp, obj_node_info_t &node){
    ALLOCA(char, desc_buf, node.size);

    pier_desc_t *desc = new pier_desc_t();

    fread(desc_buf, node.size, 1, fp);

    char * p = desc_buf;

    //read version
    uint16 sub_version = decode_uint16(p);
    sub_version &=  0x3FFF;
    sub_version >>= 8;
    assert(sub_version>=3);
    desc->price = decode_uint32(p);
    desc->maintenance = decode_uint32(p);
    desc->intro_date = decode_uint16(p);
    desc->retire_date = decode_uint16(p);
    desc->max_weight = decode_uint32(p);
    desc->max_altitude = decode_sint8(p);

    uint8 above_way_ribi = decode_uint8(p);
    desc->above_way_ribi = above_way_ribi & 0xF;
    desc->tooltipflags_a = above_way_ribi >> 4;
    uint8 below_way_ribi = decode_uint8(p);
    desc->below_way_ribi = below_way_ribi & 0xF;
    desc->tooltipflags_m = below_way_ribi >> 4;

    desc->base_mask = decode_uint32(p);
    desc->support_mask = decode_uint32(p);
    desc->sub_obj_mask = decode_uint32(p);
    if(desc->sub_obj_mask==0){
        desc->sub_obj_mask=0xFFFFFFFF;
    }else if(desc->sub_obj_mask==0xFFFFFFFF){
        desc->sub_obj_mask=0;
    }

    desc->deck_obj_mask = decode_uint32(p);
    desc->auto_group = decode_uint8(p);
    desc->auto_height = decode_uint8(p);
    desc->number_of_seasons=decode_uint8(p);
    desc->above_slope=decode_uint8(p);
    desc->rotational_symmetry=decode_uint8(p);
    desc->middle_mask=decode_uint32(p);
    desc->pier_weight=decode_uint32(p);
    uint8 drag_ribi=decode_uint8(p);
    desc->above_way_supplement=decode_uint8(p);

    //extract flags from upper bits of drag_ribi
    desc->keep_dry = (drag_ribi & 0x10) != 0;
    desc->bottom_only = (drag_ribi & 0x20) != 0;
    desc->drag_ribi = drag_ribi & 0x0F;
    desc->above_way_supplement = (drag_ribi & 0x40) != 0;
    desc->low_waydeck = (drag_ribi & 0x80) != 0;

    desc->topspeed = -1;
    desc->axle_load = -1;

    if(sub_version>=2){
        desc->support_mask |= (uint64)decode_uint32(p) << 32;
        desc->middle_mask |= (uint64)decode_uint32(p) << 32;
        desc->base_mask |= (uint64)decode_uint32(p) << 32;

        desc->topspeed = decode_uint16(p);
        desc->axle_load = decode_uint16(p);
    }
    if(sub_version>=3){
        desc->auto_group = decode_uint32(p);
        decode_uint32(p);
    }

    shufflemask(desc->support_mask);
    shufflemask(desc->middle_mask);
    shufflemask(desc->base_mask);

    return desc;
}
