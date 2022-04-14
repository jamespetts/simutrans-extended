/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "pier_rotation_select.h"
#include "../descriptor/pier_desc.h"

#include "gui_frame.h"
#include "../simworld.h"
#include "../simtool.h"
#include "simwin.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_numberinput.h"


///Modified from station building gui, may be able to merge with it

static const char label_text[4][8] = {
    "south",
    "east",
    "north",
    "west"
};

tool_build_pier_t pier_rotation_select_t::tool=tool_build_pier_t();

pier_rotation_select_t::pier_rotation_select_t(const pier_desc_t *desc) :
    gui_frame_t( translator::translate("Choose direction") ){

    this->desc = desc;

	set_table_layout(1,0);
	set_alignment(ALIGN_CENTER_H);

	// the image array
	add_table(2,2);
	{
		for( sint16 i=0;  i<4;  i++ ) {
			add_table(1,0)->set_alignment(ALIGN_CENTER_H);
			{
				image_id img = desc->get_background(pier_desc_t::rotation_select_image,i,0);
				if(img==IMG_EMPTY){
					img=desc->get_background(0,i,0);
				}
				new_component<gui_image_t>(img,0,ALIGN_NONE,true);

				actionbutton[i].init( button_t::roundbox | button_t::flexible, translator::translate(label_text[i]) );
				actionbutton[i].add_listener(this);
				add_component(&actionbutton[i]);
			}
			end_table();
		}

	}
	end_table();

	reset_min_windowsize();
}

bool pier_rotation_select_t::action_triggered(gui_action_creator_t *comp, value_t v){
	for(int i=0; i<4; i++) {
		if(comp == &actionbutton[i]  ||  v.i == i) {
			static cbuffer_t default_str;
			default_str.clear();
			default_str.printf("%s,%i", desc->get_name(), i );
			tool.set_default_param(default_str);
			welt->set_tool( &tool, welt->get_active_player() );
			destroy_win(this);
		}
	}
	return true;
}

///modified from signal spacing frame

sint8 pier_height_select_t::height = 2;

pier_height_select_t::pier_height_select_t(player_t *player_,tool_build_pier_auto_t *tool_) : gui_frame_t(translator::translate("set pier height")){
	player = player_;
	tool = tool_;
	height = tool->get_height(player);

	set_table_layout(3,0);

	new_component<gui_label_t>("Starting Height");
	new_component<gui_fill_t>();

	height_inp.add_listener(this);
	int max_height=8;
	if(tool->get_desc()){
		if(int mh = tool->get_desc()->get_max_altitude()){
			max_height=mh;
		}
	}
	height_inp.init(2,0,max_height);
	add_component( &height_inp );

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
}

bool pier_height_select_t::action_triggered(gui_action_creator_t *comp, value_t){
	if(comp == &height_inp){
		height = height_inp.get_value();
	}
	tool->set_height(player,height);
	return true;
}
