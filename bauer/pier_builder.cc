/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string.h>

#include "pier_builder.h"
#include "../descriptor/pier_desc.h"
#include "../obj/pier.h"
#include "../boden/pier_deck.h"

#include "../simtool.h"

#include "../dataobj/scenario.h"
#include "../gui/tool_selector.h"
#include "../obj/gebaeude.h"
#include "../descriptor/building_desc.h"

karte_ptr_t pier_builder_t::welt;

stringhashtable_tpl<pier_desc_t *, N_BAGS_MEDIUM> pier_builder_t::desc_table;

static bool compare_piers(const pier_desc_t* a, const pier_desc_t* b){
    return a->get_auto_group() < b->get_auto_group();
}

koord3d pier_builder_t::lookup_deck_pos(const grund_t *gr){
    sint8 hdiff;
    if(gr->get_grund_hang()==0){
        hdiff=1;
    }else{
        hdiff=slope_t::max_diff(gr->get_grund_hang());
    }
    return gr->get_pos()+koord3d(0,0,hdiff);
}

const char * pier_builder_t::check_below_ways(player_t *player, koord3d pos, const pier_desc_t *desc, const uint8 rotation, bool halfheight){
    const grund_t *gr = welt->lookup(pos);
    if(gr){
        if(weg_t *w0 = gr->get_weg_nr(0)){
            if(desc->get_above_way_ribi() && gr->get_weg_hang()){
                return "Placing a pier here would block way(s)";
            }
            if(desc->get_low_waydeck()){
                return "Placing a pier here would block way(s)";
            }
            if( ( w0->get_ribi_unmasked() & desc->get_below_way_ribi(rotation) ) == w0->get_ribi_unmasked()
                    || ( (halfheight) && w0->is_low_clearence(player) ) ){
                if(weg_t *w1  = gr->get_weg_nr(1)){
                    if( ( w1->get_ribi_unmasked() & desc->get_below_way_ribi(rotation) ) == w1->get_ribi_unmasked()
                            || ( (halfheight) && w1->is_low_clearence(player) ) ){
                        return NULL;
                    }
                }else{
                    return NULL;
                }
            }
        }else{
            return NULL;
        }
    }else{
        return NULL;
    }
    return "Placing a pier here would block way(s)";
}

const char * pier_builder_t::check_for_buildings(const grund_t *gr, const pier_desc_t *desc, const uint8){
    gebaeude_t *gb = gr->get_building();
    uint8 floor=0;
    if(!gb){
        gr=welt->lookup(gr->get_pos()-koord3d(0,0,1));
        if(gr){
            gb = gr->get_building();
            floor=1;
        }
    }
    if(!gb){
        return NULL;
    }

    const building_tile_desc_t* tile = gb->get_tile();

    //mask allows any buildings
    if(desc->get_sub_obj_mask()==0xFFFFFFFF){
        //same general restrictions as existing elevated ways
        if(gb->is_attraction() || gb->is_townhall()){
            return "Cannot build piers on attractions or townhalls";
        }

        if(tile->get_desc()->get_level() > welt->get_settings().get_max_elevated_way_building_level()){
            return "Cannot build pier on large buildings";
        }

        return NULL;
    }else if(desc->get_sub_obj_mask()==0){
        return "Cannot build this type of pier on buildings";
    }else{
        //filter buildings

        uint32 building_mask=tile->get_desc()->get_pier_mask(floor);

        if(pier_t::check_sub_masks(desc->get_sub_obj_mask(),building_mask)){
            return NULL;
        }
        return "Cannot build this type of pier on this type of building";
    }

    return NULL;
}

void pier_builder_t::register_desc(pier_desc_t *desc){
    if (const pier_desc_t *old_pier = desc_table.remove(desc->get_name())) {
        dbg->doubled("pier", desc->get_name());
        tool_t::general_tool.remove( old_pier->get_builder() );
        if(old_pier->get_auto_builder()){
            tool_t::general_tool.remove(old_pier->get_auto_builder());
            delete old_pier->get_auto_builder();
        }
        delete old_pier->get_builder();
        delete old_pier;
    }

    //add the tool(s)
    tool_build_pier_t *tool = new tool_build_pier_t;
    tool->set_icon( desc->get_cursor()->get_image_id(1) );
    tool->cursor = desc->get_cursor()->get_image_id(0);
    tool->set_default_param(desc->get_name());
    tool_t::general_tool.append( tool );
    desc->set_builder( tool );

    if(desc->get_background(pier_desc_t::auto_tool_icon_image,0,0)!=IMG_EMPTY){
        tool_build_pier_auto_t *tool = new tool_build_pier_auto_t;
        tool->set_icon( desc->get_background(pier_desc_t::auto_tool_icon_image,0,0));
        tool->cursor = desc->get_background(pier_desc_t::auto_tool_cursor_image,0,0);
        tool->set_default_param(desc->get_name());
        tool_t::general_tool.append( tool );
        desc->set_auto_builder( tool );
    }else{
        desc->set_auto_builder(NULL);
    }

    for(int i=0; i < 3; i++){
        if(desc->get_true_background(pier_desc_t::alt_tool_icon,i+1,0)!=IMG_EMPTY){
            tool = new tool_build_pier_t;
            tool->set_icon(desc->get_true_background(pier_desc_t::alt_tool_icon,i+1,0));
            desc->ref_tool_string(i).clear();
            desc->ref_tool_string(i).printf("%s,%d",desc->get_name(),i+1);
            tool->set_default_param(desc->ref_tool_string(i));
            tool_t::general_tool.append(tool);
            desc->set_alt_tool(i,tool);
        }
    }

    desc_table.put(desc->get_name(), desc);
}

const pier_desc_t *pier_builder_t::get_desc(const char *name){
    if(name){
        if(desc_table.is_contained(name)){
            return desc_table.get(name);
        }
    }
    return NULL;
}

const pier_desc_t *pier_builder_t::get_desc_bad_load(koord3d pos,player_t *owner,uint8 &rotation){
    pier_finder_params params;
    grund_t *gr = welt->lookup(pos);
    grund_t *deck = welt->lookup(lookup_deck_pos(gr));
    if(deck){
        params.above_slope=deck->get_weg_hang();
        if(deck->get_weg_nr(0)){
            params.above_way_ribi|=deck->get_weg_nr(0)->get_ribi_unmasked();
            if(deck->get_weg_nr(1)){
                params.above_way_ribi|=deck->get_weg_nr(1)->get_ribi_unmasked();
            }
        }
        //special code to handle possible other unloaded piers
        for(uint8 i = 0; i < deck->get_top(); i++){
            obj_t *ob = deck->obj_bei(i);
            if(ob->get_typ()==obj_t::pier){
                if(!((pier_t*)ob)->is_bad_load()){
                    params.support_needed|=((pier_t*)ob)->get_base_mask();
                }
            }else if(ob->get_typ()==obj_t::way){
                params.deck_obj_present|= ((weg_t*)ob)->get_deckmask();
            }
        }
    }
    for(uint8 i = 0; i < gr->get_top(); i++){
        obj_t *ob = gr->obj_bei(i);
        if(ob->get_typ()==obj_t::pier){
            if(!((pier_t*)ob)->is_bad_load()){
                params.middle_mask_taken|=((pier_t*)ob)->get_middle_mask();
            }
        }
    }
    if(gr->get_weg_nr(0)){
        if(gr->get_weg_hang()){
            params.need_clearence=true;
        }
        params.below_way_ribi|=gr->get_weg_nr(0)->get_ribi_unmasked();
        params.need_clearence|=!gr->get_weg_nr(0)->is_low_clearence(owner);
        if(gr->get_weg_nr(1)){
            params.below_way_ribi|=gr->get_weg_nr(1)->get_ribi_unmasked();
            params.need_clearence|=!gr->get_weg_nr(1)->is_low_clearence(owner);
        }
    }
    if(gr->get_typ()==grund_t::pierdeck){
        params.allow_low_waydeck=true;
        const grund_t *gr2 = pier_t::ground_below(gr);
        if(gr2){
            if(gr2->get_weg_nr(0)){
                params.below_way_ribi|=gr2->get_weg_nr(0)->is_low_clearence(owner) ? 0 : gr2->get_weg_nr(0)->get_ribi_unmasked();
                params.allow_low_waydeck&=gr2->get_weg_nr(0)->is_low_clearence(owner);
                if(gr2->get_weg_nr(1)){
                    params.below_way_ribi|=gr2->get_weg_nr(1)->is_low_clearence(owner) ? 0 : gr2->get_weg_nr(1)->get_ribi_unmasked();
                    params.allow_low_waydeck&=gr2->get_weg_nr(1)->is_low_clearence(owner);
                }
            }
            //need special code here, pier data may not be complete
            for(uint8 i = 0; i < gr2->get_top(); i++){
                obj_t *ob = gr2->obj_bei(i);
                if(ob->get_typ()==obj_t::pier){
                    if(!((pier_t*)ob)->is_bad_load()){
                        params.support_avail|=((pier_t*)ob)->get_support_mask();
                    }
                }
            }
            if(gebaeude_t *gb = gr2->get_building()){
                params.sub_obj_present=gb->get_tile()->get_desc()->get_pier_mask(1);
            }
        }
        params.on_deck=true;
    }else{
        params.ground_slope=gr->get_grund_hang();
        if(gebaeude_t *gb = gr->get_building()){
            params.sub_obj_present=gb->get_tile()->get_desc()->get_pier_mask(0);
        }
    }

    if(gr->is_water()){
        params.is_wet=true;
    }


    if(gebaeude_t *gb = gr->get_building()){
        params.sub_obj_present=gb->get_tile()->get_desc()->get_pier_mask(1);
    }
    const pier_desc_t *desc;
    get_desc_context(desc,rotation,params,true);
    return desc;
}

void pier_builder_t::get_params_from_pos(pier_finder_params &params, koord3d pos, player_t *owner){
    const grund_t* gr=welt->lookup(pos);
    params.pos_lsbs=((pos.x&1)<<0) | ((pos.y&1)<<1) | ((pos.y&1)<<2);
    params.altitude=pos.z - welt->lookup_hgt(pos.get_2d());
    if(gr){
        if(gr->get_typ()==grund_t::brueckenboden || gr->get_typ()==grund_t::monorailboden){
            params.notallowed=true;
        }
        if(gr->is_water()){
            params.nodeck=true;
        }
        params.ground_slope=gr->get_grund_hang();
        params.is_wet=gr->is_water();
        params.on_deck=gr->get_typ()==grund_t::pierdeck;
        params.middle_mask_taken=pier_t::get_middle_mask_total(gr);
        params.existing_above_ribi=pier_t::get_above_ribi_total(gr,true);
        if(gr->get_typ()!=grund_t::pierdeck){
            params.allow_low_waydeck=false;
        }else{
            params.allow_low_waydeck=params.existing_above_ribi==0;
        }
        if(const grund_t *gr0= welt->lookup(pier_builder_t::lookup_deck_pos(gr))){
            for(uint8 i=0; i < gr0->get_top(); i++){
                if(gr0->obj_bei(i)->get_typ()==obj_t::pier){
                    if(((pier_t*)gr0->obj_bei(i))->get_low_waydeck()){
                        params.nodeck=true;
                    }
                }
            }
        }
    }
    if(const grund_t *gr2=pier_t::ground_below(pos)){
        if(gr2->is_water()){
            params.allow_low_waydeck=false;
        }
        params.support_avail=pier_t::get_support_mask_total(gr2);
        if(gr2->get_weg_nr(0)){
            params.below_way_ribi|=(gr2->get_weg_nr(0)->is_low_clearence(owner) || gr2->get_weg_hang())? 0 : gr2->get_weg_nr(0)->get_ribi_unmasked();
            if(gr2->get_weg_nr(1)){
                params.below_way_ribi|=(gr2->get_weg_nr(1)->is_low_clearence(owner) || gr2->get_weg_hang()) ? 0 : gr2->get_weg_nr(1)->get_ribi_unmasked();
            }
            if(gr2->get_weg_hang()){
                params.need_clearence=true;
            }
        }
        if(gebaeude_t *gb = gr2->get_building()){
            params.sub_obj_present=gb->get_tile()->get_desc()->get_pier_mask(1);
        }
    }
    if(gr){
        if(gr->get_weg_nr(0)){
            params.need_clearence=!gr->get_weg_nr(0)->is_low_clearence(owner);
            params.below_way_ribi|=gr->get_weg_nr(0)->get_ribi_unmasked();
            if(gr->get_weg_nr(1)){
                params.below_way_ribi|=!gr->get_weg_nr(1)->get_ribi_unmasked();
                params.need_clearence|=gr->get_weg_nr(1)->is_low_clearence(owner);
            }
            if(gr->get_weg_hang()){
                params.need_clearence=true;
            }
        }
        if(gebaeude_t *gb = gr->get_building()){
            params.sub_obj_present=gb->get_tile()->get_desc()->get_pier_mask(0);
        }
    }
    if(welt->lookup_hgt(pos.get_2d()) < welt->get_water_hgt(pos.get_2d())){
        params.notallowed=true;
    }
}

void pier_builder_t::get_params_from_ground(pier_finder_params &params, const grund_t *gr, player_t *owner){
    get_params_from_pos(params,gr->get_pos(),owner);
    return;
}

bool pier_builder_t::get_desc_from_tos(const pier_desc_t *&tos, uint8 &rotation, koord3d pos, player_t *owner, sint8 topz, bool upper_layer, ribi_t::ribi alt_tos_ribi){
    vector_tpl<pier_finder_match> top_options;
    pier_finder_params params_top;
    const pier_desc_t* old_tos=tos;
    uint8 old_rotation=rotation;

    const grund_t *gr=welt->lookup(pos);

    params_top.above_way_ribi=tos->get_above_way_ribi(rotation);
    if(alt_tos_ribi){
        params_top.above_way_ribi=alt_tos_ribi;
    }
    params_top.min_axle_load=tos->get_max_axle_load();
    params_top.autogroup=tos->get_auto_group();
    params_top.requre_low_waydeck=tos->get_low_waydeck();
    if(params_top.requre_low_waydeck){
        params_top.below_way_ribi=tos->get_below_way_ribi(rotation);
    }
    params_top.deck_obj_present=tos->get_deck_obj_mask();
    if(upper_layer){
        get_params_from_ground(params_top,gr,owner);
    }else if(topz - pos.z == 1){
        //check ways and buildings for second layer
        if(gr->get_weg_nr(0)){
            params_top.below_way_ribi|=gr->get_weg_nr(0)->is_low_clearence(owner) ? 0 : gr->get_weg_nr(0)->get_ribi_unmasked();
            if(gr->get_weg_nr(1)){
                params_top.below_way_ribi|=gr->get_weg_nr(1)->is_low_clearence(owner) ? 0 : gr->get_weg_nr(1)->get_ribi_unmasked();
            }
        }
        if(gebaeude_t *gb = gr->get_building()){
            params_top.sub_obj_present=gb->get_tile()->get_desc()->get_pier_mask(1);
        }
    }

    //check top for other piers on top tile
    koord3d top_pos=pos;
    top_pos.z = topz;
    if(const grund_t *gr_top = welt->lookup(top_pos)){
        params_top.middle_mask_taken=pier_t::get_middle_mask_total(gr_top);
    }

    pier_finder_match best_match;
    best_match.match=-1;
    best_match.desc=old_tos;
    best_match.rotation=old_rotation;

    if(get_desc_context(tos,rotation,params_top,false,&top_options)){
        if(upper_layer){
            return true;
        }
        //traverse options for top to find stackable pillar piers
        for(uint32 i = 0; i < top_options.get_count(); i++){
            pier_finder_match top=top_options[i];
            pier_finder_params params;
            params.stackable=true;
            params.support_needed=top.desc->get_base_mask(top.rotation);
            params.autogroup=top.desc->get_auto_group();
            get_params_from_ground(params,gr,owner);

            get_desc_context(tos,rotation,params,false,0,&best_match,top.desc->get_maintenance() * 4 + top.match);
        }
    }
    //no substiture found, restore original tos
    tos=best_match.desc;
    rotation=best_match.rotation;
    return best_match.match!=(uint32)(-1);
}

bool pier_builder_t::append_route_stack(vector_tpl<pier_route_elem> &route, player_t *player, const pier_desc_t *base_desc, koord3d topdeck, ribi_t::ribi deckribi){
    //check for runways
    karte_t::runway_info ri=welt->check_nearby_runways(topdeck.get_2d());
    if(ri.pos!=koord::invalid){
        return false;
    }

    //check for existing piers in right place
    if(const grund_t* gr = welt->lookup(topdeck)){
        if((deckribi & pier_t::get_above_ribi_total(gr,false)) == deckribi){
            return true;
        }
    }

    const grund_t *base_gr = welt->lookup_kartenboden(topdeck.get_2d());
    if(!base_gr) return false;
    if(!base_desc->get_low_waydeck()){
        topdeck.z-=1;
    }
    pier_route_elem elem;
    elem.desc=base_desc;
    elem.rotation=0;
    elem.pos=koord3d(topdeck.get_2d(),base_gr->get_pos().z);

    //no pier needed
    if(base_gr->get_pos().z>topdeck.z){
        return true;
    }
    if(base_gr->get_pos()==topdeck && base_desc->get_low_waydeck()){
        return true;
    }

    //trivial case of single pier
    if(base_gr->get_pos()==topdeck
            || (base_gr->get_pos()==topdeck-koord3d(0,0,1) && slope_t::max_diff(base_gr->get_grund_hang())==2)){
        if(get_desc_from_tos(elem.desc,elem.rotation,elem.pos,player,topdeck.z,true,deckribi)){
            route.append(elem);
            return true;
        }
        return false;
    }



    vector_tpl< vector_tpl<pier_finder_match> > match_tree;
    vector_tpl<pier_finder_match> match_row;
    match_tree.set_count(topdeck.z - base_gr->get_pos().z + 1);

    pier_finder_params top_params;
    if(base_desc->get_low_waydeck()){
        top_params.allow_low_waydeck=true;
        top_params.requre_low_waydeck=true;
        top_params.below_way_ribi=deckribi;
    }else{
        top_params.above_way_ribi=deckribi;
    }
    get_params_from_pos(top_params,topdeck,player);
    top_params.autogroup=base_desc->get_auto_group();
    top_params.on_deck=true;
    top_params.min_axle_load=base_desc->get_max_axle_load();
    top_params.support_avail=-1;
    top_params.deck_obj_present=base_desc->get_deck_obj_mask();

    get_desc_context(elem.desc,elem.rotation,top_params,false,&(match_tree[0]),0,0);
    for(uint8 i = 1; i < match_tree.get_count(); i++){
        koord3d row_pos = topdeck - koord3d(0,0,i);
        uint32 lasti=i-1;
        const grund_t* gr_below = welt->lookup(row_pos - koord3d(0,0,1));
        if(gr_below){
            if(slope_t::max_diff(gr_below->get_grund_hang())==2){
                row_pos=row_pos - koord3d(0,0,1);
                i++;
            }
        }
        for(uint32 j=0; j < match_tree[lasti].get_count(); j++){
            //if null match, continue link and continue
            if(match_tree[lasti][j].desc==NULL){
                pier_finder_match n;
                n.desc=NULL;
                n.match=match_tree[lasti][j].match;
                n.aux=j;
                match_tree[i].append(n);
                continue;
            }
            uint64 existingsupport=0;
            //if buildable already, add null match
            if(const grund_t *gr = welt->lookup(row_pos + koord3d(0,0,1))){
                //only thing not already tested is existing support
                if(const grund_t *gr2=pier_t::ground_below(gr)){
                    existingsupport=pier_t::get_support_mask_total(gr2);
                }
                if((existingsupport | match_tree[lasti][j].desc->get_base_mask(match_tree[lasti][j].rotation))==existingsupport){
                    pier_finder_match n;
                    n.desc=NULL;
                    n.match=match_tree[lasti][j].match;
                    n.aux=j;
                    match_tree[i].append(n);
                }
            }

            //search through to find piers that can go below
            pier_finder_params params;
            get_params_from_pos(params,row_pos,player);
            params.autogroup=match_tree[lasti][j].desc->get_auto_group();
            params.support_avail=-1;
            params.support_needed=match_tree[lasti][j].desc->get_base_mask(match_tree[lasti][j].rotation) & ~existingsupport;
            match_row.clear();
            get_desc_context(elem.desc,elem.rotation,params,false,&match_row,0,match_tree[lasti][j].match + 4 * match_tree[lasti][j].desc->get_maintenance());
            //append results, removing duplacates and marking path upwards
            //list sizes too small to justify sorting techneques
            for(uint32 k = 0; k < match_row.get_count(); k++){
                bool dup_match=false;
                for(uint32 l = 0; l<match_tree[i].get_count(); l++){
                    if(match_tree[i][l].desc==match_row[k].desc && match_tree[i][l].rotation==match_row[k].rotation){
                        dup_match=true;
                        if(match_tree[i][l].match>match_row[k].match){
                            match_tree[i][l].match=match_row[k].match;
                            match_tree[i][l].aux=j;
                        }
                        break;
                    }
                }
                if(!dup_match){
                    match_row[k].aux=j;
                    match_tree[i].append(match_row[k]);
                }
            }
        }
        //no row found
        if(match_tree[i].empty()){
            return false;
        }
    }

    uint8 match_link=0xFF;

    for(uint32 i=match_tree.get_count()-1; i < match_tree.get_count(); i--){
        if(match_link==0xFF){
            uint32 min_match=-1;
            for(uint32 j=0; j < match_tree[i].get_count(); j++){
                if(match_tree[i][j].match < min_match){
                    min_match=match_tree[i][j].match;
                    match_link=j;
                }
            }
        }
        if(match_link!=0xFF && match_tree[i].get_count()){
            if(match_tree[i][match_link].desc){
                elem.desc=match_tree[i][match_link].desc;
                elem.rotation=match_tree[i][match_link].rotation;
                elem.pos=topdeck - koord3d(0,0,i);
                route.append(elem);
            }
            match_link=match_tree[i][match_link].aux;
        }
    }

    return true;

}

bool pier_builder_t::calc_route(vector_tpl<pier_route_elem> &route, player_t *player, const pier_desc_t *base_desc, koord3d start, koord3d end, sint8 start_height){
    route.clear();
    if(start_height<0){
        return false;
    }
    if(start.get_2d()==end.get_2d()){
        return false;
    }

    start = start + koord3d(0,0,start_height);
    //use algorithm simular to way_builder calc_straight_route to find 2d route
    koord pos=start.get_2d();
    vector_tpl<koord> straight_route;
    while(pos!=end.get_2d()){
        straight_route.append(pos);
        if(abs(pos.x-end.x)>=abs(pos.y-end.y)) {
            if(pos.x>end.x){
                pos.x--;
            }else{
                pos.x++;
            }
        }
        else {
            if(pos.y>end.y){
                pos.y--;
            }else{
                pos.y++;
            }
        }
    }
    straight_route.append(end.get_2d());
    vector_tpl<ribi_t::ribi> route_ribi(straight_route.get_size());
    route_ribi.append(ribi_type(straight_route[1]-straight_route[0]));
    for(uint32 i = 1; i < straight_route.get_count()-1; i++){
        ribi_t::ribi pos_ribi = ribi_t::none;
        pos_ribi|=ribi_type(straight_route[i-1]-straight_route[i]);
        pos_ribi|=ribi_type(straight_route[i+1]-straight_route[i]);
        route_ribi.append(pos_ribi);
    }
    if(straight_route.get_count()>=2){
        ribi_t::ribi pos_ribi = ribi_t::none;
        pos_ribi|=ribi_type(straight_route[straight_route.get_count()-2]-straight_route[straight_route.get_count()-1]);
        if(ribi_t::is_bend(route_ribi[straight_route.get_count()-2])){
            pos_ribi=~route_ribi[straight_route.get_count()-2] & 0xF;
        }
        route_ribi.append(pos_ribi);
    }else{
        route_ribi.append(ribi_type(straight_route[straight_route.get_count()-2]-straight_route[straight_route.get_count()-1]));
    }

    for(uint32 i = 0; i < straight_route.get_count(); i++){
        if(!append_route_stack(route,player,base_desc,koord3d(straight_route[i],start.z),route_ribi[i])){
            return false;
        }
    }

    return true;
}

bool pier_builder_t::get_desc_context(pier_desc_t const *& descriptor, uint8& rotation, pier_finder_params params, bool allow_inexact, vector_tpl<pier_finder_match> *matches, pier_finder_match *best_match, uint32 add_match){
    descriptor=NULL;

    sint32 min_cost=0x7FFFFFFF;
    uint32 min_match=-1;
    if(params.existing_above_ribi==0 && ribi_t::is_single(params.above_way_ribi)){
        params.above_way_ribi=ribi_t::doubles(params.above_way_ribi);
    }
    if(params.requre_low_waydeck && ribi_t::is_single(params.below_way_ribi)){
        params.below_way_ribi=ribi_t::doubles(params.below_way_ribi);
    }
    if(params.requre_low_waydeck && !params.allow_low_waydeck){
        return false;
    }
    if(params.notallowed && !allow_inexact){
        return false;
    }

    bool exact_match=params.notallowed;
    for(auto const & i : desc_table){
        pier_desc_t const* const desc = i.value;

        if(!(desc->get_auto_group() & params.autogroup)){
            continue;
        }

        for(uint8 r=0; r<4; r++){
            if(desc->get_background(params.ground_slope,r,0) == IMG_EMPTY){
                continue;
            }

            uint32 match=0;
            bool unmatch=false;

            if(!desc->is_available(welt->get_timeline_year_month())){
                unmatch=true;
                match+=32;
            }

            if(desc->get_max_altitude() && params.altitude > desc->get_max_altitude()){
                unmatch=true;
                match+=2;
            }

            ribi_t::ribi way_ribi=params.requre_low_waydeck ? params.below_way_ribi : params.above_way_ribi;
            if(ribi_t::is_straight_ns(way_ribi)){
                match+=(r&2)^(params.pos_lsbs&2);
            }
            if(ribi_t::is_straight_ew(way_ribi)){
                match+=(((r&2)>>1)^(params.pos_lsbs&1))<<1;
            }

            if(params.stackable &&
                    (desc->get_support_mask(r)|desc->get_base_mask(r)) != desc->get_support_mask(r)){
                unmatch=true;
                match+=hammingWeight(desc->get_support_mask(r) ^ desc->get_base_mask(r));
            }

            if(params.max_cost< desc->get_value()){
                unmatch=true;
                match+=(desc->get_value()-params.max_cost)/128;
            }

            if(params.max_maintenance < desc->get_maintenance()){
                unmatch=true;
                match+=(desc->get_maintenance()-params.max_maintenance)/128;
            }

            if(params.min_axle_load > desc->get_max_axle_load()){
                unmatch=true;
                match+=params.min_axle_load - desc->get_max_axle_load();
            }

            if(params.requre_low_waydeck && !desc->get_low_waydeck()){
                unmatch=true;
                match+=256;
            }


            if(params.existing_above_ribi==0 && desc->get_above_way_supplement()){
                match+=64;
                unmatch=true;
            }

            if(params.nodeck && desc->get_above_way_ribi()){
                match+=128;
                unmatch=true;
            }

            params.above_way_ribi &= ~params.existing_above_ribi;
            if((params.above_way_ribi & desc->get_above_way_ribi(r)) != params.above_way_ribi){
                unmatch=true;
            }
            match+=hammingWeight((uint8)(params.above_way_ribi ^ desc->get_above_way_ribi(r)));

            if((params.below_way_ribi & desc->get_below_way_ribi(r)) != params.below_way_ribi){
                unmatch=true;
            }
            match+=hammingWeight((uint8)(params.below_way_ribi ^ desc->get_below_way_ribi(r)));

            if(params.above_slope!= desc->get_above_slope(r)){
                match+=128;
                unmatch=true;
            }

            if(params.on_deck){
                if((params.support_avail | desc->get_base_mask(r))!= params.support_avail){
                    unmatch=true;
                }
                match+=hammingWeight(params.support_avail ^ desc->get_base_mask(r));

                if(desc->get_bottom_only()){
                    match+=2;
                    unmatch=true;
                }
            }

            if((params.support_needed & desc->get_support_mask(r)) != params.support_needed){
                unmatch=true;
            }
            match+=hammingWeight(params.support_needed ^ desc->get_support_mask(r));

            if((~params.middle_mask_taken | desc->get_middle_mask(r)) != ~params.middle_mask_taken){
                unmatch=true;
            }
            match+=hammingWeight(~params.middle_mask_taken & desc->get_middle_mask(r));

            if((params.deck_obj_present & desc->get_deck_obj_mask()) != params.deck_obj_present){
                unmatch=true;
            }
            match+=hammingWeight(params.deck_obj_present ^ desc->get_deck_obj_mask());

            if((params.sub_obj_present & desc->get_sub_obj_mask()) != desc->get_sub_obj_mask()){
                match+=4;
                unmatch=true;
            }
            match+=hammingWeight(params.sub_obj_present ^ desc->get_sub_obj_mask());

            if(params.need_clearence && !desc->get_above_way_supplement() && desc->get_above_way_ribi(r)){
                match+=64;
                unmatch=true;
            }

            if(!params.requre_low_waydeck && !params.allow_low_waydeck && desc->get_low_waydeck()){
                match+=128;
                unmatch=true;
            }

            if(params.is_wet && desc->get_keep_dry()){
                match+=1;
                unmatch=true;
            }

            if(desc->get_auto_height_avoid()){
                match *= 2;
            }

            if(!unmatch && !exact_match){
                exact_match=true;
                min_cost=desc->get_maintenance();
                min_match=match;
                descriptor=desc;
                rotation=r;
            }
            if(exact_match && !unmatch){
                if(desc->get_maintenance() < min_cost){
                    min_cost=desc->get_maintenance();
                    min_match=match;
                    descriptor=desc;
                    rotation=r;
                }else if(desc->get_maintenance() == min_cost && match < min_match){
                    min_cost=desc->get_maintenance();
                    min_match=match;
                    descriptor=desc;
                    rotation=r;
                }
            }
            if(!unmatch){
                if(matches){
                    pier_finder_match m;
                    m.desc=desc;
                    m.rotation=r;
                    m.match=match+add_match;
                    matches->append(m);

                }
                if(best_match){
                    if(match+add_match < best_match->match || (best_match->desc && match+add_match == best_match->match && desc->get_maintenance() < best_match->desc->get_maintenance())){
                        best_match->match=match+add_match;
                        best_match->desc=desc;
                        best_match->rotation=rotation;
                    }
                }
            }
            if(!exact_match && unmatch){
                if(match < min_match){
                    min_match = match;
                    descriptor=desc;
                    rotation=r;
                    min_cost=desc->get_maintenance();
                }
            }
        }
    }
    if(!exact_match && !allow_inexact){
        descriptor=NULL;
    }
    return exact_match;
}

const char *pier_builder_t::build(player_t *player, koord3d pos, const pier_desc_t *desc, const uint8 rotation){
    grund_t *gr = NULL;

    gr = welt->lookup(pos);
    if(!gr) return "Invalid Ground";

    if(desc->get_background(gr->get_grund_hang(),rotation,0) == IMG_EMPTY){
        return "Invalid ground for this type of pier";
    }

    if(welt->lookup_hgt(pos.get_2d()) < welt->get_water_hgt(pos.get_2d())){
        return "Cannot build in deep water";
    }

    if(desc->get_max_altitude() && pos.z - welt->lookup_hgt(pos.get_2d()) > desc->get_max_altitude()){
        return "Cannot build this pier this far above grade";
    }

    if(gr->is_water() && desc->get_above_way_ribi()){
        return "Pier could block shipping lanes";
    }

    if(const grund_t *gr2 = pier_t::ground_below(gr)){
        if(gr2->is_water() && desc->get_low_waydeck()){
            return "Pier could block shipping lanes";
        }
    }

    if(gr->is_water() && desc->get_keep_dry()){
        return "This type of pier cannot be built on water";
    }

    if(gr->get_typ()==grund_t::brueckenboden || gr->get_typ()==grund_t::monorailboden){
        return "Pier can not be built on bridges";
    }

    //check for ways
    const char *msg;
    msg = check_below_ways(player,pos,desc,rotation,false);
    if(msg){
        return msg;
    }
    if(welt->get_settings().get_way_height_clearance()==2){
        msg = check_below_ways(player,pos + koord3d(0,0,-1),desc,rotation,true);
        if(msg){
            return msg;
        }
        if(desc->get_above_way_ribi()){
            if(weg_t *w0 = gr->get_weg_nr(0)){
                if(!w0->is_low_clearence(player) || desc->get_low_waydeck()){
                    return "Placing a pier here would block way(s)";
                }
            }
            if(weg_t *w1 = gr->get_weg_nr(1)){
                if(!w1->is_low_clearence(player)){
                    return "Placing a pier here would block way(s)";
                }
            }
        }
    }

    //check for buildings
    msg = check_for_buildings(gr,desc,rotation);
    if(msg){
        return msg;
    }

    //check lower pier has enough supporting area
    if(gr->get_typ()==grund_t::pierdeck){
        if(desc->get_bottom_only()){
            return "This type of pier needs to be built on solid ground";
        }
        grund_t *gr2=welt->lookup(gr->get_pos() + koord3d(0,0,-1));
        if(gr2==NULL){
            gr2=welt->lookup(gr->get_pos() + koord3d(0,0,-2));
        }
        uint64 supportmask=pier_t::get_support_mask_total(gr2);
        if((supportmask & desc->get_base_mask(rotation))!=desc->get_base_mask(rotation)){
            return "Cannot build pier.  Lower pier does not provide sufficient footing";
        }
    }else if(desc->get_low_waydeck()){
        return "Cannot build this pier on ground";
    }

    if(desc->get_middle_mask(rotation) & pier_t::get_middle_mask_total(gr)){
        return "Cannot build pier.  New pier would occupy same space as existing pier";
    }


    //make ground on top (if needed)
    koord3d gpos=lookup_deck_pos(gr);
    if(!welt->lookup(gpos)){
        if(!desc->get_low_waydeck()){
            pier_deck_t *deck = new pier_deck_t(gpos,desc->get_above_slope(rotation),desc->get_above_slope(rotation));
            welt->access(pos.get_2d())->boden_hinzufuegen(deck);
            deck->calc_image();
        }
    }else if(desc->get_above_way_ribi()){
        const grund_t *gr=welt->lookup(gpos);
        for(uint8 i=0; i < gr->get_top(); i++){
            if(gr->obj_bei(i)->get_typ()==obj_t::pier){
                if(((pier_t*)gr->obj_bei(i))->get_low_waydeck()){
                    return "Cannot place pier here due to redundant deck";
                }
            }
        }
    }

    if(!desc->get_low_waydeck() && desc->get_above_way_ribi()){
        grund_t *deck=welt->lookup(gpos);
        //update all other parapets
        ribi_t::ribi full_ribi = pier_t::get_above_ribi_total(deck) | desc->get_above_way_ribi(rotation);
        for(uint i=0; i < deck->get_top(); i++){
            if(deck->obj_bei(i)->get_typ()==obj_t::parapet){
                ((parapet_t*)deck->obj_bei(i))->update_extra_ways(full_ribi);
            }
        }
        //create new parapet if available
        if(desc->get_background(pier_desc_t::parapet[0],0,0)!=IMG_EMPTY){
            parapet_t *parapet=new parapet_t(deck->get_pos(),player,desc,rotation);
            deck->obj_add(parapet);
        }
    }

    //remove trees
    if(gr->ist_natur()) {
        player_t::book_construction_costs(player, -gr->remove_trees(), gr->get_pos().get_2d());
    }

    //create pier
    pier_t *p = new pier_t(pos,player,desc,rotation);
    gr->obj_add(p);

    return NULL;
}

const char *pier_builder_t::remove(player_t *player, koord3d pos){
    grund_t *gr;
    gr = welt->lookup(pos);
    if(!gr) return "Invalid Ground";

    const char *msg=0;
    uint8 pier_cnt=0;
    pier_t *p = NULL;
    for(uint8 i = 0; i < gr->get_top(); i++){
        obj_t *ob = gr->obj_bei(i);
        if(ob->get_typ()==obj_t::pier){
            pier_cnt++;
            p=(pier_t*)ob;
        }
    }

    if(pier_cnt==0){
        return "No piers here";
    }

    if(pier_cnt==1){
        if(p->get_low_waydeck() && gr->get_weg_nr(0)){
            return "Cannot remove pier supporting way";
        }
        koord3d gpos=lookup_deck_pos(gr);
        grund_t *bd = welt->lookup(gpos);
        if(bd){
            if(bd->get_top()){
                if(bd->get_top()==1 && bd->obj_bei(0)->get_typ()==obj_t::parapet){
                    obj_t* parapet=bd->obj_bei(0);
                    parapet->cleanup(player);
                    gr->obj_remove(parapet);
                    delete parapet;
                }else{
                    return "Cannot remove sole load bearing pier";
                }
            }
        }
        p->cleanup(player);
        gr->obj_remove(p);
        delete p;
        if(bd){
            welt->access(gpos.get_2d())->boden_entfernen(bd);
            delete bd;
        }
        return NULL;
    }

    for(uint8 j = 0; j < gr->get_top(); j++){ //try every pier in tile
        obj_t *ob = gr->obj_bei(j);
        if(ob->get_typ()==obj_t::pier){
            pier_cnt++;
            p = (pier_t*)ob;
            if(p->is_deletable(player)==NULL){
                parapet_t* parapet = NULL;
                if(p->get_low_waydeck() && gr->get_weg_nr(0)){
                    continue;
                }
                koord3d gpos=lookup_deck_pos(gr);
                grund_t *bd = welt->lookup(gpos);
                //check that we are not supporting another pier
                const char *msg2=NULL;
                for(uint8 i = 0; i < bd->get_top(); i++){
                    if(bd->obj_bei(i)->get_typ()==obj_t::pier){
                        if(((pier_t*)bd->obj_bei(i))->get_base_mask() & p->get_support_mask()){
                            msg2= "Cannot remove load bearing pier";
                            break;
                        }
                    }
                    if(bd->obj_bei(i)->get_typ()==obj_t::way){
                        weg_t *way=(weg_t*)bd->obj_bei(i);
                        if(!p->get_desc()->get_above_way_supplement() || (way->get_ribi_unmasked() & p->get_above_ribi())){
                            msg2= "Cannot remove load bearing pier";
                            break;
                        }
                    }
                    if(bd->obj_bei(i)->get_typ()==obj_t::parapet){
                        parapet_t* tpar=(parapet_t*)bd->obj_bei(i);
                        if(tpar->get_desc()==p->get_desc() && tpar->get_rotation() == p->get_rotation()){
                            parapet=tpar;
                        }
                    }
                }
                if(msg2) {
                    if(msg){
                        msg="No piers can be safely removed";
                    }else{
                        msg=msg2;
                    }
                    continue;
                }

                if(parapet){
                    parapet->cleanup(player);
                    bd->obj_remove(parapet);
                    delete parapet;
                }

                p->cleanup(player);

                gr->obj_remove(p);
                delete p;


                return NULL;

            }
        }
    }

    return msg;
}

void pier_builder_t::fill_menu(tool_selector_t *tool_selector, char mode, uint32 filter){
    if(!welt->get_scenario()->is_tool_allowed(welt->get_active_player(), TOOL_BUILD_PIER | GENERAL_TOOL)){
        return;
    }

    const uint16 time = welt->get_timeline_year_month();
    vector_tpl<const pier_desc_t*> matching(desc_table.get_count());

    for(auto const & i :desc_table){
        pier_desc_t const * const p = i.value;
        if(p->is_available(time)){
            if(!filter || (filter&p->get_deck_obj_mask())){
                matching.insert_ordered(p, compare_piers);
            }
        }
    }

    FOR(vector_tpl<pier_desc_t const*>, const i, matching){
        if(mode!='A'){
            tool_selector->add_tool_selector(i->get_builder());
            for(int j=0; j < 3; j++){
                if(i->get_alt_tool(j)){
                    tool_selector->add_tool_selector(i->get_alt_tool(j));
                }
            }
        }
        if(i->get_auto_builder() && mode!='M'){
            tool_selector->add_tool_selector(i->get_auto_builder());
        }
    }

}
