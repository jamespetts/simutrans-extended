/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Defines all button types: Normal (roundbox), Checkboxes (square), Arrows, Scrollbars
 */

#include "gui_button.h"

#include "../../simcolor.h"
#include "../../display/simgraph.h"
#include "../../simevent.h"
#include "../simwin.h"

#include "../../sys/simsys.h"

#include "../../dataobj/translator.h"

#include "../../simskin.h"
#include "../../descriptor/skin_desc.h"
#include "../../utils/simstring.h"

// the following are only needed for the posbutton ...
#include "../../simworld.h"
#include "../../boden/grund.h"
#include "../../display/viewport.h"
#include "../../obj/zeiger.h"

#include "../gui_frame.h"

#define STATE_BIT (button_t::state)
#define AUTOMATIC_BIT (button_t::automatic)

#define get_state_offset() (b_enabled ? pressed : 2)


karte_ptr_t button_t::welt;


button_t::button_t() :
	gui_component_t(true)
{
	b_no_translate = false;
	pressed = false;
	translated_tooltip = tooltip = NULL;
	background_color = color_idx_to_rgb(COL_WHITE);
	b_enabled = true;
	img = IMG_EMPTY;

	// By default a box button
	init(box,"");
}


void button_t::init(enum type type_par, const char *text_par, scr_coord pos_par, scr_size size_par)
{
	translated_tooltip = NULL;
	tooltip = NULL;
	b_no_translate = ( type_par==posbutton );

	set_typ(type_par);
	set_text(text_par);
	set_pos(pos_par);
	if(  size_par != scr_size::invalid  ) {
		set_size(size_par);
	}
}


// set type. this includes size for specified buttons.
void button_t::set_typ(enum type t)
{
	type = t;
	text_color = SYSCOL_BUTTON_TEXT;
	switch (type&TYPE_MASK) {

		case square:
			text_color = SYSCOL_CHECKBOX_TEXT;
			if(  !strempty(translated_text)  ) {
				set_text(translated_text);
				set_size( scr_size( gui_theme_t::gui_checkbox_size.w + D_H_SPACE + proportional_string_width( translated_text ), max(gui_theme_t::gui_checkbox_size.h,LINESPACE)) );
			}
			else {
				set_size( scr_size( gui_theme_t::gui_checkbox_size.w, max(gui_theme_t::gui_checkbox_size.h,LINESPACE)) );
			}
			break;

		case arrowleft:
		case repeatarrowleft:
			set_size( gui_theme_t::gui_arrow_left_size );
			break;

		case posbutton:
			set_no_translate( true );
			set_size( gui_theme_t::gui_pos_button_size );
			break;

		case arrowright:
		case repeatarrowright:
			set_size( gui_theme_t::gui_arrow_right_size );
			break;

		case arrowup:
			set_size( gui_theme_t::gui_arrow_up_size );
			break;

		case arrowdown:
			set_size( gui_theme_t::gui_arrow_down_size );
			break;

		case box:
			text_color = SYSCOL_COLORED_BUTTON_TEXT;
			/* FALLTHROUGH */
		case roundbox:
		case roundbox_left:
		case roundbox_middle:
		case roundbox_right:
			set_size( scr_size(get_size().w, max(D_BUTTON_HEIGHT, LINESPACE)));
			break;

		case imagebox:
			set_size( scr_size(max(get_size().w,LINESPACE), max(get_size().w,LINESPACE)) );
			img = IMG_EMPTY;
			break;

		case sortarrow:
		{
			const uint8 block_height = max(size.h/7,2);
			const uint8 bars_height = uint8((size.h-block_height-4)/4) * block_height*2 + block_height;
			set_size( scr_size(max(D_BUTTON_HEIGHT, (gui_theme_t::gui_color_button_text_offset.w+4)*2 + 6/*arrow width(5)+margin(1)*/+block_height + (bars_height-2)/2), max(D_BUTTON_HEIGHT, LINESPACE)) );
			b_no_translate = false;
			break;
		}

		case depot:
		{
			b_no_translate = false;
			text_color = color_idx_to_rgb(91); // brown
			targetpos = koord3d::invalid;
			break;
		}


		default:
			break;
	}
	update_focusability();
}


void button_t::set_targetpos( const koord k )
{
	targetpos.x = k.x;
	targetpos.y = k.y;
	targetpos.z = welt->max_hgt( k );
}


scr_size button_t::get_max_size() const
{
	switch(type&TYPE_MASK) {
		case square:
		case box:
		case roundbox:
		case roundbox_left:
		case roundbox_middle:
		case roundbox_right:
			return (type & flexible) ? scr_size(scr_size::inf.w, get_min_size().h) : get_min_size();

		default:
			return get_min_size();
	}
}

scr_size button_t::get_min_size() const
{

	switch (type&TYPE_MASK) {
		case arrowleft:
		case repeatarrowleft:
			return gui_theme_t::gui_arrow_left_size;

		case posbutton:
			return gui_theme_t::gui_pos_button_size;

		case arrowright:
		case repeatarrowright:
			return gui_theme_t::gui_arrow_right_size;

		case arrowup:
			return gui_theme_t::gui_arrow_up_size;

		case arrowdown:
			return gui_theme_t::gui_arrow_down_size;

		case square: {
			scr_coord_val w = translated_text ?  D_H_SPACE + proportional_string_width( translated_text ) : 0;
			return scr_size(w + gui_theme_t::gui_checkbox_size.w, max(gui_theme_t::gui_checkbox_size.h,LINESPACE));
		}
		case box:
		case roundbox:
		case roundbox_left:
		case roundbox_middle:
		case roundbox_right: {
			scr_coord_val w = translated_text ?  2*D_H_SPACE + D_BUTTON_PADDINGS_X + proportional_string_width( translated_text ) : 0;
			scr_size size = type & flexible ?
				scr_size(gui_theme_t::gui_button_size.w, max(D_BUTTON_HEIGHT, LINESPACE)) : scr_size (get_size().w, max(D_BUTTON_HEIGHT,LINESPACE));
			if (img) {
				scr_coord_val x = 0, y = 0, img_w = 0, h = 0;
				display_get_image_offset(img, &x, &y, &img_w, &h);
				w += img_w+2;
			}
			size.w = max(size.w, w);
			return size;
		}

		case imagebox: {
			scr_coord_val x = 0, y = 0, w = 0, h = 0;
			display_get_image_offset(img, &x, &y, &w, &h);
			return scr_size(max(get_size().w, w + 4), max(get_size().h, h + 4));
		}

		case sortarrow:
		{
			const uint8 block_height = max(size.h/7,2);
			const uint8 bars_height = uint8((size.h-block_height-4)/4) * block_height*2 + block_height;
			return scr_size( max( D_BUTTON_HEIGHT, (gui_theme_t::gui_color_button_text_offset.w+4)*2 + 6/*arrow width(5)+margin(1)*/+block_height + (bars_height-2)/2 ), max(D_BUTTON_HEIGHT, LINESPACE) );
		}

		case swap_vertical:
			return scr_size(D_BUTTON_PADDINGS_X + 18/*arrow width(5*2)+center margin(3)+padding*/ ,D_LABEL_HEIGHT);

		case depot:
		{
			return scr_size( max(18, D_BUTTON_HEIGHT), max(D_BUTTON_HEIGHT, LINESPACE));
		}

		default:
			return gui_component_t::get_min_size();
	}
}

/**
 * Sets the text displayed in the button
 */
void button_t::set_text(const char * text)
{
	this->text = text;
	translated_text = b_no_translate ? text : translator::translate(text);

	if(  (type & TYPE_MASK) == square  &&  !strempty(translated_text)  ) {
		set_size( scr_size( gui_theme_t::gui_checkbox_size.w + D_H_SPACE + proportional_string_width( translated_text ), max(gui_theme_t::gui_checkbox_size.h, LINESPACE)) );
	}
}


/**
 * Sets the tooltip of this button
 */
void button_t::set_tooltip(const char * t)
{
	if(  t == NULL  ) {
		translated_tooltip = tooltip = NULL;
	}
	else {
		tooltip = t;
		translated_tooltip = b_no_translate ? tooltip : translator::translate(tooltip);
	}
}


bool button_t::getroffen(scr_coord p)
{
	bool hit=gui_component_t::getroffen(p);
	if(  pressed  &&  !hit  &&  ( (type & STATE_BIT) == 0)  ) {
		// moved away
		pressed = 0;
	}
	return hit;
}


/**
 * Event responder
 */
bool button_t::infowin_event(const event_t *ev)
{
	if(  ev->ev_class==INFOWIN  &&  ev->ev_code==WIN_OPEN  ) {
		if(text) {
			translated_text = b_no_translate ? text : translator::translate(text);
		}
		if(tooltip) {
			translated_tooltip = b_no_translate ? tooltip : translator::translate(tooltip);
		}
	}

	if(  ev->ev_class==EVENT_KEYBOARD  ) {
		if(  ev->ev_code==32  &&  get_focus()  ) {
			// space toggles button
			call_listeners( (long)0 );
			return true;
		}
		return false;
	}

	// we ignore resize events, they shouldn't make us pressed or unpressed
	if(!b_enabled  ||  IS_WINDOW_RESIZE(ev)) {
		return false;
	}

	// check if the initial click and the current mouse positions are within the button's boundary
	bool const cxy_within_boundary = 0 <= ev->click_pos.x && ev->click_pos.x < get_size().w && 0 <= ev->click_pos.y && ev->click_pos.y < get_size().h;
	bool const mxy_within_boundary = 0 <= ev->mouse_pos.x && ev->mouse_pos.x < get_size().w && 0 <= ev->mouse_pos.y && ev->mouse_pos.y < get_size().h;

	// update the button pressed state only when mouse positions are within boundary or when it is mouse release
	if(  (type & STATE_BIT) == 0  &&  cxy_within_boundary  &&  (  mxy_within_boundary  ||  IS_LEFTRELEASE(ev)  )  ) {
		pressed = (ev->button_state==1);
	}

	// make sure that the button will take effect only when the mouse positions are within the component's boundary
	if(  !cxy_within_boundary  ||  !mxy_within_boundary  ) {
		return false;
	}

	if(IS_LEFTRELEASE(ev)) {
		if(  (type & TYPE_MASK)==posbutton  ||  type==depot_automatic  ) {
			call_listeners( &targetpos );
			if( type==posbutton_automatic  ||  type==depot_automatic ) {
				welt->get_viewport()->change_world_position( targetpos );
				welt->get_zeiger()->change_pos( targetpos );
			}
			return true;
		}
		else {
			if(  type & AUTOMATIC_BIT  ) {
				pressed = !pressed;
			}
			call_listeners( (long)0 );
			return true;
		}
	}
	else if(  (type & TYPE_MASK) >= repeatarrowleft  &&  ev->button_state==1  ) {
		uint32 cur_time = dr_time();
		if (IS_LEFTCLICK(ev)  ||  button_click_time==0) {
			button_click_time = cur_time;
		}
		else if(cur_time - button_click_time > 100) {
			// call listerner every 100 ms
			call_listeners( (long)1 );
			button_click_time = cur_time;
			return true;
		}
	}
	else if( IS_RIGHTRELEASE(ev) ) {
		if( (type&TYPE_MASK)==depot  &&  targetpos!=koord3d::invalid ) {
			world()->get_viewport()->change_world_position( targetpos );
		}
	}
	return false;
}


void button_t::draw_focus_rect( scr_rect r, scr_coord_val offset) {

	scr_coord_val w = ((offset-1)<<1);

	display_fillbox_wh_clip_rgb(r.x-offset+1,     r.y-1-offset+1,    r.w+w, 1, color_idx_to_rgb(COL_WHITE), false);
	display_fillbox_wh_clip_rgb(r.x-offset+1,     r.y+r.h+offset-1,  r.w+w, 1, color_idx_to_rgb(COL_WHITE), false);
	display_vline_wh_clip_rgb  (r.x-offset,       r.y-offset+1,      r.h+w,    color_idx_to_rgb(COL_WHITE), false);
	display_vline_wh_clip_rgb  (r.x+r.w+offset-1, r.y-offset+1,      r.h+w,    color_idx_to_rgb(COL_WHITE), false);
}


// draw button. x,y is top left of window.
void button_t::draw(scr_coord offset)
{
	if(  !is_visible()  ) {
		return;
	}

	const scr_rect area( offset+pos, size );
	PIXVAL text_color = pressed ? SYSCOL_BUTTON_TEXT_SELECTED : this->text_color;
	text_color = b_enabled ? text_color : SYSCOL_BUTTON_TEXT_DISABLED;

	switch (type&TYPE_MASK) {

		case box:      // Colored background box
		case roundbox: // button with inside text
		case roundbox_left:
		case roundbox_middle:
		case roundbox_right:
		case imagebox:
			{
				switch (type&TYPE_MASK) {
					case box:
					case imagebox:
						display_img_stretch(gui_theme_t::button_tiles[get_state_offset()], area);
						display_img_stretch_blend(gui_theme_t::button_color_tiles[b_enabled && pressed], area, background_color | TRANSPARENT75_FLAG | OUTLINE_FLAG);
						break;
					case roundbox_left:
						display_img_stretch(gui_theme_t::round_button_left_tiles[get_state_offset()], area);
						break;
					case roundbox_middle:
						display_img_stretch(gui_theme_t::round_button_middle_tiles[get_state_offset()], area);
						break;
					case roundbox_right:
						display_img_stretch(gui_theme_t::round_button_right_tiles[get_state_offset()], area);
						break;
					default:
						display_img_stretch(gui_theme_t::round_button_tiles[get_state_offset()], area);
						break;
				}

				scr_coord_val x = 0, y = 0, w = 0, h = 0;
				if(  img  ) {
					display_get_image_offset(img, &x, &y, &w, &h);
				}
				scr_rect area_img  = scr_rect(area.x, area.y, w>0?w+4:0, area.h);
				scr_rect area_text = area - gui_theme_t::gui_button_text_offset_right;
				area_img.set_pos( area.get_pos() );
				area_text.set_pos( gui_theme_t::gui_button_text_offset + area.get_pos() );
				if(  text  ) {
					if( img != IMG_EMPTY ) {
						area_text.w -= (w+D_H_SPACE);
						if (!img_on_right) {
							area_text.x += w;
						}
						else {
							area_img.x += area_text.w;
						}
					}
					if( type&box && pressed ) {
						text_color = SYSCOL_COLORED_BUTTON_TEXT_SELECTED;
					}
					// move the text to leave evt. space for a colored box top left or bottom right of it
					if( pressed && gui_theme_t::pressed_button_sinks ) area_text.y++;
					display_proportional_ellipsis_rgb( area_text, translated_text, ALIGN_CENTER_H | ALIGN_CENTER_V | DT_CLIP, text_color, true );
				}
				if(  img != IMG_EMPTY  ) {
					if(  text  ) {
						if ( !img_on_right ) {
							area_img.x += gui_theme_t::gui_button_text_offset.w;
						}
					}
					else {
						// image on center
						area_img=area;
					}
					if( pressed && gui_theme_t::pressed_button_sinks ) area_img.y++;
					display_img_aligned( img, (type&TYPE_MASK)==imagebox ? area : area_img, ALIGN_CENTER_H | ALIGN_CENTER_V | DT_CLIP, true );
				}
				if(  win_get_focus()==this  ) {
					draw_focus_rect( area );
				}
			}
			break;

		case sortarrow:
			{
				display_img_stretch(gui_theme_t::button_tiles[0], area);

				const uint8 block_height = max(size.h/7,2);
				const uint8 bars_height = min(size.h-2, block_height*5+2);
				const uint8 rows = (uint8)(bars_height/block_height)/2+1;
				const uint8 min_bar_width = max(((size.w-8)/rows)>>1, 2);
				const uint8 max_bar_width = min_bar_width*rows;
				scr_rect area_drawing(area.x, area.y, 6/*arrow width(5)+margin(1)*/+ max_bar_width, bars_height);
				area_drawing.set_pos(area.get_pos() + scr_coord(D_GET_CENTER_ALIGN_OFFSET((6+max_bar_width),area.w),D_GET_CENTER_ALIGN_OFFSET(bars_height,size.h)));

				// draw an arrow
				display_fillbox_wh_clip_rgb(area_drawing.x+2, area_drawing.y, 1, bars_height, SYSCOL_BUTTON_TEXT, false);
				if (pressed) {
					// desc
					display_fillbox_wh_clip_rgb(area_drawing.x+1, area_drawing.y+1, 3, 1, SYSCOL_BUTTON_TEXT, false);
					display_fillbox_wh_clip_rgb(area_drawing.x,   area_drawing.y+2, 5, 1, SYSCOL_BUTTON_TEXT, false);
					for (uint8 row=0; row*block_height*2<bars_height; row++) {
						display_fillbox_wh_clip_rgb(area_drawing.x + 6/*arrow width(5)+margin(1)*/, area_drawing.y + bars_height - block_height - row*block_height*2, min_bar_width*(row+1), block_height, SYSCOL_BUTTON_TEXT, false);
					}
					tooltip = "hl_btn_sort_desc";
				}
				else {
					// asc
					display_fillbox_wh_clip_rgb(area_drawing.x+1, area_drawing.y+bars_height-2, 3, 1, SYSCOL_BUTTON_TEXT, false);
					display_fillbox_wh_clip_rgb(area_drawing.x,   area_drawing.y+bars_height-3, 5, 1, SYSCOL_BUTTON_TEXT, false);
					for (uint8 row=0; row*block_height*2<bars_height; row++) {
						display_fillbox_wh_clip_rgb(area_drawing.x + 6/*arrow width(5)+margin(1)*/, area_drawing.y + row*block_height*2 + 1, min_bar_width*(row+1), block_height, SYSCOL_BUTTON_TEXT, false);
					}
					tooltip = "hl_btn_sort_asc";
				}

				if(  getroffen(get_mouse_pos() - offset)  ) {
					translated_tooltip = translator::translate(tooltip);
				}
			}
			break;

		case swap_vertical:
			{
				display_img_stretch(gui_theme_t::round_button_tiles[get_state_offset()], area);

				scr_coord_val xoff = area.x + gui_theme_t::gui_button_text_offset.w + 5;
				const scr_coord_val yoff = area.y + 2 + (pressed&&gui_theme_t::pressed_button_sinks);
				// up arrow
				display_fillbox_wh_clip_rgb(xoff,   yoff,   1, D_LABEL_HEIGHT-4, SYSCOL_BUTTON_TEXT, false);
				display_fillbox_wh_clip_rgb(xoff-1, yoff+1, 3, 1, SYSCOL_BUTTON_TEXT, false);
				display_fillbox_wh_clip_rgb(xoff-2, yoff+2, 5, 1, SYSCOL_BUTTON_TEXT, false);

				// down arrow
				xoff = area.x + size.w - gui_theme_t::gui_button_text_offset.w - 6;
				display_fillbox_wh_clip_rgb(xoff,   yoff, 1, D_LABEL_HEIGHT-4, SYSCOL_BUTTON_TEXT, false);
				display_fillbox_wh_clip_rgb(xoff-1, yoff+D_LABEL_HEIGHT-6, 3, 1, SYSCOL_BUTTON_TEXT, false);
				display_fillbox_wh_clip_rgb(xoff-2, yoff+D_LABEL_HEIGHT-7, 5, 1, SYSCOL_BUTTON_TEXT, false);

			}
			break;

		case depot:
			{
				display_img_stretch(gui_theme_t::round_button_tiles[get_state_offset()], area);
				if (background_color != color_idx_to_rgb(COL_WHITE)) {
					display_img_stretch_blend(gui_theme_t::button_color_tiles[b_enabled && pressed], area, background_color | TRANSPARENT75_FLAG | OUTLINE_FLAG);
				}
				const PIXVAL depot_color = b_enabled ? this->text_color : SYSCOL_BUTTON_TEXT_DISABLED;
				display_depot_symbol_rgb(area.x+3, area.y+2+(pressed&&gui_theme_t::pressed_button_sinks), size.w-6, depot_color, false);
			}
			break;

		case square: // checkbox with text
			{
				display_img_aligned( gui_theme_t::check_button_img[ get_state_offset() ], area, ALIGN_CENTER_V, true );
				if(  text  ) {
					text_color = b_enabled ? this->text_color : SYSCOL_CHECKBOX_TEXT_DISABLED;
					scr_rect area_text = area;
					area_text.x += gui_theme_t::gui_checkbox_size.w + D_H_SPACE;
					area_text.w -= gui_theme_t::gui_checkbox_size.w + D_H_SPACE;
					display_proportional_ellipsis_rgb( area_text, translated_text, ALIGN_LEFT | ALIGN_CENTER_V | DT_CLIP, text_color, true );
				}
				if(  win_get_focus() == this  ) {
					draw_focus_rect( scr_rect( area.get_pos()+scr_coord(0,(area.get_size().h-gui_theme_t::gui_checkbox_size.w)/2), gui_theme_t::gui_checkbox_size ) );
				}
			}
			break;

		case posbutton:
			{
				uint8 offset = get_state_offset();
				if(  offset == 0  ) {
					if(  grund_t *gr = welt->lookup_kartenboden(targetpos.get_2d())  ) {
						offset = welt->get_viewport()->is_on_center( gr->get_pos() );
					}
				}
				display_img_aligned( gui_theme_t::pos_button_img[ offset ], area, ALIGN_CENTER_H|ALIGN_CENTER_V, true );
			}
			break;

		case arrowleft:
		case repeatarrowleft:
			display_img_aligned( gui_theme_t::arrow_button_left_img[ get_state_offset() ], area, ALIGN_CENTER_H|ALIGN_CENTER_V, true );
			break;

		case arrowright:
		case repeatarrowright:
			display_img_aligned( gui_theme_t::arrow_button_right_img[ get_state_offset() ], area, ALIGN_CENTER_H|ALIGN_CENTER_V, true );
			break;

		case arrowup:
			display_img_aligned( gui_theme_t::arrow_button_up_img[ get_state_offset() ], area, ALIGN_CENTER_H|ALIGN_CENTER_V, true );
			break;

		case arrowdown:
			display_img_aligned( gui_theme_t::arrow_button_down_img[ get_state_offset() ], area, ALIGN_CENTER_H|ALIGN_CENTER_V, true );
			break;

		default: ;
	}

	if(  getroffen(get_mouse_pos() - offset)  ) {
		win_set_tooltip( scr_coord{ get_mouse_pos().x, area.get_bottom() } + TOOLTIP_MOUSE_OFFSET, translated_tooltip, this);
	}
}


void button_t::update_focusability()
{
	switch (type&TYPE_MASK) {

		case box:      // Old, 4-line box
		case roundbox: // New box with round corners
		case square:   // Little square in front of text (checkbox)
			set_focusable(true);
			break;

		// those cannot receive focus ...
		case imagebox:
		case sortarrow:
		case swap_vertical:
		case arrowleft:
		case repeatarrowleft:
		case arrowright:
		case repeatarrowright:
		case posbutton:
		case arrowup:
		case arrowdown:
		case roundbox_left:
		case roundbox_middle:
		case roundbox_right:
		default:
			set_focusable(false);
			break;
	}
}
