/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "../simunits.h"
#include "../simdebug.h"
#include "simobj.h"
#include "../display/simimg.h"
#include "../player/simplay.h"
#include "../simtool.h"
#include "../simworld.h"
#include "../simsignalbox.h"

#include "../descriptor/roadsign_desc.h"
#include "../descriptor/skin_desc.h"
#include "../descriptor/building_desc.h"

#include "../boden/grund.h"
#include "../boden/wege/strasse.h"

#include "../dataobj/loadsave.h"
#include "../dataobj/scenario.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "../gui/trafficlight_info.h"
#include "../gui/privatesign_info.h"
#include "../gui/onewaysign_info.h"
#include "../gui/tool_selector.h"

#include "../tpl/stringhashtable_tpl.h"

#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"

#include "roadsign.h"
#include "signal.h"

#include "../simconvoi.h"
#include "../vehicle/rail_vehicle.h"

const roadsign_desc_t *roadsign_t::default_signal = NULL;

stringhashtable_tpl<roadsign_desc_t *, N_BAGS_MEDIUM> roadsign_t::table;
vector_tpl<roadsign_desc_t*> roadsign_t::list;


#ifdef INLINE_OBJ_TYPE
roadsign_t::roadsign_t(typ type, loadsave_t *file) : obj_t (type)
{
	init(file);
}

roadsign_t::roadsign_t(loadsave_t *file) : obj_t (obj_t::roadsign)
{
	init(file);
}

void roadsign_t::init(loadsave_t *file)
#else
roadsign_t::roadsign_t(loadsave_t *file) : obj_t ()
#endif
{
	image = foreground_image = IMG_EMPTY;
	preview = false;
	rdwr(file);
	if(desc) {
		/* if more than one state, we will switch direction and phase for traffic lights
		 * however also gate signs need indications
		 */
		automatic = (desc->get_count()>4  &&  desc->get_wtyp()==road_wt)  ||  (desc->get_count()>2  &&  desc->is_private_way());
	}
	else {
		automatic = false;
	}
	// some sve had rather strange entries in state
	if(  !automatic  ||  desc==NULL  ) {
		state = 0;
	}
	// only traffic light need switches
	if(  automatic  ) {
		welt->sync.add(this);
	}
}

#ifdef INLINE_OBJ_TYPE

roadsign_t::roadsign_t(typ type, player_t *player, koord3d pos, ribi_t::ribi dir, const roadsign_desc_t* desc, bool preview) : obj_t(type, pos)
{
	init(player, dir, desc, preview);
}

roadsign_t::roadsign_t(player_t *player, koord3d pos, ribi_t::ribi dir, const roadsign_desc_t *desc, bool preview) : obj_t(obj_t::roadsign, pos)
{
	init(player, dir, desc, preview);
}

void roadsign_t::init(player_t *player, ribi_t::ribi dir, const roadsign_desc_t *desc, bool preview)
#else
roadsign_t::roadsign_t(player_t *player, koord3d pos, ribi_t::ribi dir, const roadsign_desc_t *desc, bool preview) : obj_t(pos)
#endif
{
#ifdef MULTI_THREAD_CONVOYS
	if (env_t::networkmode)
	{
		// In network mode, we cannot have a sign being created concurrently with
		// convoy path-finding because  whether the convoy path-finder is called
		// on this tile of way before or after this function is indeterminate.
		world()->await_convoy_threads();
	}
#endif
#ifdef MULTI_THREAD
	welt->await_private_car_threads();
#endif
	this->desc = desc;
	this->dir = dir;
	this->preview = preview;
	image = foreground_image = IMG_EMPTY;
	state = 0;
	ticks_ns = ticks_ow = 16;
	ticks_offset = 0;
	ticks_amber_ns = ticks_amber_ow = 2;
	lane_affinity = 4;
	set_owner( player );
	if(  desc->is_private_way()  ) {
		// init ownership of private ways
		ticks_ns = ticks_ow = 0;
		if(  player->get_player_nr() >= 8  ) {
			ticks_ow = 1 << (player->get_player_nr()-8);
		}
		else {
			ticks_ns = 1 << player->get_player_nr();
		}
	}
	/* if more than one state, we will switch direction and phase for traffic lights
	 * however also gate signs need indications
	 */
	automatic = (desc->get_count()>4  &&  desc->get_wtyp()==road_wt)  ||  (desc->get_count()>2  &&  desc->is_private_way());
	// only traffic light need switches
	if(  automatic  ) {
		welt->sync.add(this);
	}
}


roadsign_t::~roadsign_t()
{
	if(  desc  ) {
		const grund_t *gr = welt->lookup(get_pos());
		if(gr) {
			weg_t *weg = gr->get_weg(desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt);
			if(weg) {
				if (!preview) {
					player_t* owner = get_owner();
					if(owner)
					{
						// Remove maintenance cost
						sint32 maint = get_desc()->get_maintenance();
						player_t::add_maintenance(owner, -maint, weg->get_waytype());
					}
					if (desc->is_single_way()  ||  desc->is_signal_type()) {
						// signal removed, remove direction mask
						weg->set_ribi_maske(ribi_t::none);
					}
					weg->clear_sign_flag();
				}
			}
			else {
				dbg->error("roadsign_t::~roadsign_t()","roadsign_t %p was deleted but ground has no way of type %d!", this, desc->get_wtyp() );
			}
		}
	}
	if(automatic) {
		welt->sync.remove(this);
	}
}


void roadsign_t::set_dir(ribi_t::ribi dir)
{
	ribi_t::ribi olddir = this->dir;

	this->dir = dir;
	if (!preview) {
		weg_t *weg = welt->lookup(get_pos())->get_weg(desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt);
		if(  desc->get_wtyp()!=track_wt  &&  desc->get_wtyp()!=monorail_wt  &&  desc->get_wtyp()!=maglev_wt  &&  desc->get_wtyp()!=narrowgauge_wt  ) {
			weg->count_sign();
		}
		if(  desc->is_single_way()  ||  desc->is_signal()  ||  desc->is_pre_signal()  ||  desc->is_longblock_signal()  ) {
			// set mask, if it is a single way ...
			weg->count_sign();
			weg->set_ribi_maske(calc_mask());
DBG_MESSAGE("roadsign_t::set_dir()","ribi %i",dir);
		}
	}

	// force redraw
	mark_image_dirty(get_image(),0);
	// some more magic to get left side images right ...
	sint8 old_x = get_xoff();
	set_xoff( after_xoffset );
	mark_image_dirty(foreground_image,after_yoffset-get_yoff());
	set_xoff( old_x );

	image = IMG_EMPTY;
	foreground_image = IMG_EMPTY;
	calc_image();

	if (preview)
	{
		this->dir = olddir;
	}
}


void roadsign_t::show_info()
{
	if(  desc->is_private_way()  ) {
		create_win(new privatesign_info_t(this), w_info, (ptrdiff_t)this );
	}
	else if(  automatic  ) {
		create_win(new trafficlight_info_t(this), w_info, (ptrdiff_t)this );
	}
	else if(  desc->is_single_way()  ) {
		if(  (intersection_pos = get_intersection()) == koord3d::invalid  ) {
			set_lane_affinity(4);
			obj_t::show_info();
		}
		else {
			// off the "not applied" bit flag
			lane_affinity = ~((~lane_affinity)|4);
			create_win(new onewaysign_info_t(this, intersection_pos), w_info, (ptrdiff_t)this );
		}
	}
	else {
		obj_t::show_info();
	}
}


void roadsign_t::info(cbuffer_t & buf) const
{
	obj_t::info(buf);

	buf.append(translator::translate(desc->get_name()));
	buf.append("\n\n");

	if (desc->is_choose_sign())
	{
		buf.append(translator::translate("choose_sign"));
		buf.append("\n");
	}
	if (desc->is_traffic_light())
	{
		buf.append(translator::translate("traffic_light"));
		buf.append("\n");
	}
	if (desc->is_single_way())
	{
		buf.append(translator::translate("one_way_sign"));
		buf.append("\n");
	}
	if (desc->is_private_way())
	{
		buf.append(translator::translate("private_way_sign"));
		buf.append("\n");
	}
	if (desc->is_end_choose_signal())
	{
		buf.append(translator::translate("end_of_choose_sign"));
		buf.append("\n");
	}
	if (desc->get_min_speed() != 0)
	{
		buf.printf("%s%s%d%s%s", translator::translate("min_speed"), ": ", speed_to_kmh(desc->get_min_speed()), " ", "km/h");
		buf.append("\n");
	}

	koord3d rs_pos = this->get_pos();

	const grund_t* rs_gr3d = welt->lookup(rs_pos);
	const weg_t* way = rs_gr3d->get_weg(desc->get_wtyp() != tram_wt ? desc->get_wtyp() : track_wt);
	if ((uint32)way->get_max_speed() * 2U >= speed_to_kmh(desc->get_max_speed()))
	{
		buf.printf("%s%s%d%s%s", translator::translate("Max. speed:"), " ", speed_to_kmh(desc->get_max_speed()), " ", "km/h");
		buf.append("\n");

		if (way->is_rail_type())
		{
			buf.printf("%s%s%s%d%s%s%s", "(", translator::translate("track_speed"), ": ", way->get_max_speed(), " ", "km/h", ")");
		}
		else
		{
			buf.printf("%s%s%s%d%s%s%s", "(", translator::translate("way_speed"), ": ", way->get_max_speed(), " ", "km/h", ")");
		}
		buf.append("\n");
	}


	const grund_t *rs_gr = welt->lookup_kartenboden(rs_pos.x, rs_pos.y);
	if (rs_gr->get_hoehe() > rs_pos.z)
	{
		buf.append(translator::translate("underground_sign"));
		buf.append("\n");
	}

	if (desc->get_maintenance() > 0)
	{
		char maintenance_number[64];
		money_to_string(maintenance_number, (double)welt->calc_adjusted_monthly_figure(desc->get_maintenance()) / 100.0);
		buf.printf("%s%s", translator::translate("maintenance"), ": ");
		buf.append(maintenance_number);
	}
	else
	{
		buf.append(translator::translate("no_maintenance_costs"));
	}
	buf.append("\n");
	buf.append("\n");

	if (desc->is_single_way())
	{
		buf.printf("%s%s%s", translator::translate("permitted_direction"), ": ", translator::translate(get_directions_name(get_dir()))); // Perhaps: "direction_of_travel" ?
		buf.append("\n");
	}
	else if (desc->is_traffic_light())
	{
		buf.printf("%s%s%s", translator::translate("current_clear_directions"), ":\n", translator::translate(get_directions_name(get_dir()))); // Perhaps: "direction_of_travel" ?
		buf.append("\n");
	}
	else
	{
		buf.append(translator::translate("Direction"));
		buf.append(": ");
		buf.append(translator::translate(get_directions_name(get_dir())));
		buf.append("\n");
	}

	if(desc->is_single_way() && intersection_pos != koord3d::invalid) {
			buf.printf("%s:(%d,%d,%d)\n", translator::translate("intersection"), intersection_pos.x,intersection_pos.y,intersection_pos.z);
		}

	// Did not figure out how to make the sign registrate a passing train // Ves
	/*
	buf.append(translator::translate("Time since a train last passed"));
	buf.append(": ");
	char time_since_train_last_passed[32];
	welt->sprintf_ticks(time_since_train_last_passed, sizeof(time_since_train_last_passed), welt->get_zeit_ms() - rs->get_train_last_passed());
	buf.append(time_since_train_last_passed);
	buf.append("\n");
	*/
#ifdef DEBUG
	buf.append(translator::translate("\ndirection:"));
	buf.append(dir);
	buf.append("\n");
#endif
	if (desc->is_traffic_light())
	{
		if(  automatic  ) {
			buf.append(translator::translate("\nSet phases:"));
		}
		else{
			if (char const* const maker = desc->get_copyright()) {
				buf.append("\n");
				buf.printf(translator::translate("Constructed by %s"), maker);
			}
			// extra treatment of trafficlights & private signs, author will be shown by those windows themselves
		}
	}
}
	// Signal specific information is dealt with in void signal_t::info(cbuffer_t & buf, bool dummy) const (in signal.cc)


// could be still better aligned for drive_left settings ...
// now only an offset in desc could improve it ...
void roadsign_t::calc_image()
{
	set_flag(obj_t::dirty);

	// vertical offset of the signal positions
	const grund_t *gr=welt->lookup(get_pos());
	if(gr==NULL) {
		return;
	}

	after_xoffset = 0;
	after_yoffset = 0;
	sint8 xoff = 0, yoff = 0;
	// left offsets defined, and image-on-the-left activated
	const bool left_offsets = desc->get_offset_left()  &&
	    (     (desc->get_wtyp()==road_wt  &&  welt->get_settings().is_drive_left()  )
	      ||  (desc->get_wtyp()!=air_wt  &&  desc->get_wtyp()!=road_wt  &&  welt->get_settings().is_signals_left())
	    );

	const slope_t::type full_hang = gr->get_weg_hang();
	const sint8 hang_diff = slope_t::max_diff(full_hang);
	const ribi_t::ribi hang_dir = ribi_t::backward( ribi_type(full_hang) );

	// private way have also closed/open states
	if(  desc->is_private_way()  ) {
		uint8 image = 1-(dir&1);
		if(  (1<<welt->get_active_player_nr()) & get_player_mask()  ) {
			// gate open
			image += 2;
			// force redraw
		    mark_image_dirty(get_image(),0);
		}
		set_image( desc->get_image_id(image) );
		set_yoff( 0 );
		if(  hang_diff  ) {
			set_yoff( -(TILE_HEIGHT_STEP*hang_diff)/2 );
		}
		else {
			set_yoff( -gr->get_weg_yoff() );
		}
		foreground_image = IMG_EMPTY;
		return;
	}

	if(  hang_diff == 0  ) {
		yoff = -gr->get_weg_yoff();
		after_yoffset = yoff;
	}
	else {
		// since the places were switched
		const ribi_t::ribi test_hang = left_offsets ? ribi_t::backward(hang_dir) : hang_dir;
		if(test_hang==ribi_t::east ||  test_hang==ribi_t::north) {
			yoff = -TILE_HEIGHT_STEP*hang_diff;
			after_yoffset = 0;
		}
		else {
			yoff = 0;
			after_yoffset = -TILE_HEIGHT_STEP*hang_diff;
		}
	}

	image_id tmp_image=IMG_EMPTY;
	if(!automatic) {
		assert( state==0 );

		foreground_image = IMG_EMPTY;
		ribi_t::ribi temp_dir = dir;

		if(  gr->get_typ()==grund_t::tunnelboden  &&  gr->ist_karten_boden()  &&
			(grund_t::underground_mode==grund_t::ugm_none  ||  (grund_t::underground_mode==grund_t::ugm_level  &&  gr->get_hoehe()<grund_t::underground_level))   ) {
			// entering tunnel here: hide the image further in if not undergroud/sliced
			const ribi_t::ribi tunnel_hang_dir = ribi_t::backward( ribi_type(gr->get_grund_hang()) );
			if(  tunnel_hang_dir==ribi_t::east ||  tunnel_hang_dir==ribi_t::north  ) {
				temp_dir &= ~ribi_t::southwest;
			}
			else {
				temp_dir &= ~ribi_t::northeast;
			}
		}

		// signs for left side need other offsets and other front/back order
		if(  left_offsets  ) {
			const sint16 XOFF = 2*desc->get_offset_left();
			const sint16 YOFF = desc->get_offset_left();

			if(temp_dir&ribi_t::east) {
				tmp_image = desc->get_image_id(3);
				xoff += XOFF;
				yoff += -YOFF;
			}

			if(temp_dir&ribi_t::north) {
				if(tmp_image!=IMG_EMPTY) {
					foreground_image = desc->get_image_id(0);
					after_xoffset += -XOFF;
					after_yoffset += -YOFF;
				}
				else {
					tmp_image = desc->get_image_id(0);
					xoff += -XOFF;
					yoff += -YOFF;
				}
			}

			if(temp_dir&ribi_t::west) {
				foreground_image = desc->get_image_id(2);
				after_xoffset += -XOFF;
				after_yoffset += YOFF;
			}

			if(temp_dir&ribi_t::south) {
				if(foreground_image!=IMG_EMPTY) {
					tmp_image = desc->get_image_id(1);
					xoff += XOFF;
					yoff += YOFF;
				}
				else {
					foreground_image = desc->get_image_id(1);
					after_xoffset += XOFF;
					after_yoffset += YOFF;
				}
			}
		}
		else {

			if(temp_dir&ribi_t::east) {
				foreground_image = desc->get_image_id(3);
			}

			if(temp_dir&ribi_t::north) {
				if(foreground_image!=IMG_EMPTY) {
					tmp_image = desc->get_image_id(0);
				}
				else {
					foreground_image = desc->get_image_id(0);
				}
			}

			if(temp_dir&ribi_t::west) {
				tmp_image = desc->get_image_id(2);
			}

			if(temp_dir&ribi_t::south) {
				if(tmp_image!=IMG_EMPTY) {
					foreground_image = desc->get_image_id(1);
				}
				else {
					tmp_image = desc->get_image_id(1);
				}
			}
		}

		// some signs on roads must not have a background (but then they have only two rotations)
		if(  desc->get_flags()&roadsign_desc_t::ONLY_BACKIMAGE  ) {
			if(foreground_image!=IMG_EMPTY) {
				tmp_image = foreground_image;
			}
			foreground_image = IMG_EMPTY;
		}
	}
	else {
		// traffic light
		weg_t *str=gr->get_weg(road_wt);
		if(str) {
			const uint8 weg_dir = (str->get_ribi()==str->get_ribi_unmasked()) ? str->get_ribi() : str->get_ribi_unmasked()^str->get_ribi();
			const uint8 direction = desc->get_count()>16 ? (state&2)+((state+1)&1) : (state+1) & 1;

			// other front/back images for left side ...
			if(  left_offsets  ) {
				const sint16 XOFF = 2*desc->get_offset_left();
				const sint16 YOFF = desc->get_offset_left();

				if(weg_dir&ribi_t::north) {
					if(weg_dir&ribi_t::east) {
						foreground_image = desc->get_image_id(6+direction*8);
						after_xoffset += 0;
						after_yoffset += 0;
					}
					else {
						foreground_image = desc->get_image_id(1+direction*8);
						after_xoffset += XOFF;
						after_yoffset += YOFF;
					}
				}
				else if(weg_dir&ribi_t::east) {
					foreground_image = desc->get_image_id(2+direction*8);
					after_xoffset += -XOFF;
					after_yoffset += YOFF;
				}
				else {
					foreground_image = IMG_EMPTY;
				}

				if(weg_dir&ribi_t::west) {
					if(weg_dir&ribi_t::south) {
						tmp_image = desc->get_image_id(7+direction*8);
						xoff += 0;
						yoff += 0;
					}
					else {
						tmp_image = desc->get_image_id(3+direction*8);
						xoff += XOFF;
						yoff += -YOFF;
					}
				}
				else if(weg_dir&ribi_t::south) {
					tmp_image = desc->get_image_id(0+direction*8);
					xoff += -XOFF;
					yoff += -YOFF;
				}
				else {
					tmp_image = IMG_EMPTY;
				}
			}
			else {
				// drive right ...
				if(weg_dir&ribi_t::south) {
					if(weg_dir&ribi_t::east) {
						foreground_image = desc->get_image_id(4+direction*8);
					}
					else {
						foreground_image = desc->get_image_id(0+direction*8);
					}
				}
				else if(weg_dir&ribi_t::east) {
					foreground_image = desc->get_image_id(2+direction*8);
				}
				else {
					foreground_image = IMG_EMPTY;
				}

				if(weg_dir&ribi_t::west) {
					if(weg_dir&ribi_t::north) {
						tmp_image = desc->get_image_id(5+direction*8);
					}
					else {
						tmp_image = desc->get_image_id(3+direction*8);
					}
				}
				else if(weg_dir&ribi_t::north) {
					tmp_image = desc->get_image_id(1+direction*8);
				}
				else {
					tmp_image = IMG_EMPTY;
				}
			}

		}
	}
	// set image and offsets
	set_image( tmp_image );
	set_xoff( xoff );
	set_yoff( yoff );

}


// only used for traffic light: change the current state
sync_result roadsign_t::sync_step(uint32 /*delta_t*/)
{
	if(  desc->is_private_way()  ) {
		uint8 image = 1-(dir&1);
		if(  (1<<welt->get_active_player_nr()) & get_player_mask()  ) {
			// gate open
			image += 2;
			// force redraw
			mark_image_dirty(get_image(),0);
		}
		set_image( desc->get_image_id(image) );
	}
	else {
		// change every ~32s
		// Must not overflow if ticks_ns+ticks_ow+ticks_amber_ns+ticks_amber_ow=256
	        uint32 ticks = ((welt->get_ticks()>>10)+ticks_offset) % ((uint32)ticks_ns+(uint32)ticks_ow+(uint32)ticks_amber_ns+(uint32)ticks_amber_ow);

		uint8 new_state=0;
		//traffic light transition: e-w dir -> amber e-w -> n-s dir -> amber n-s -> ...
		if( ticks < ticks_ow ){
		  new_state=0;
		}else if( ticks < ticks_ow+ticks_amber_ow ){
		  new_state=2;
		}else if( ticks < ticks_ow+ticks_amber_ow+ticks_ns ){
		  new_state=1;
		}else{
		  new_state=3;
		}

		if(state!=new_state) {
			state = new_state;
			switch(new_state){
			case 0:
			  dir=ribi_t::northsouth;
			  break;
			case 1:
			  dir=ribi_t::eastwest;
			  break;
			case 2:
			case 3:
			  dir=ribi_t::none;
			  break;
			default:
			  dir=ribi_t::eastwest;
			}
			calc_image();
		}
	}
	return SYNC_OK;
}


void roadsign_t::rotate90()
{
	// only meaningful for traffic lights
	obj_t::rotate90();
	if(automatic  &&  !desc->is_private_way()) {
	  state = (state&2/*whether amber*/) + ((state+1)&1);
		if (ticks_offset >= ticks_ns) {
			ticks_offset -= ticks_ns;
		} else {
			ticks_offset += ticks_ow;
		}
		uint8 temp = ticks_ns;
		ticks_ns = ticks_ow;
		ticks_ow = temp;
		temp = ticks_amber_ns;
		ticks_amber_ns = ticks_amber_ow;
		ticks_amber_ow = temp;

		trafficlight_info_t *const trafficlight_win = dynamic_cast<trafficlight_info_t *>( win_get_magic( (ptrdiff_t)this ) );
		if(  trafficlight_win  ) {
			trafficlight_win->update_data();
		}
	}
	dir = ribi_t::rotate90( dir );
}


// to correct offset on slopes
#ifdef MULTI_THREAD
void roadsign_t::display_after(int xpos, int ypos, const sint8 clip_num ) const
#else
void roadsign_t::display_after(int xpos, int ypos, bool ) const
#endif
{
	if(  foreground_image != IMG_EMPTY  ) {
		const int raster_width = get_current_tile_raster_width();
		xpos += tile_raster_scale_x( after_xoffset, raster_width );
		ypos += tile_raster_scale_y( after_yoffset, raster_width );
		// draw with owner
		if(  get_owner_nr() != PLAYER_UNOWNED  ) {
			if(  obj_t::show_owner  ) {
				display_blend( foreground_image, xpos, ypos, 0, color_idx_to_rgb(get_owner()->get_player_color1()+2) | OUTLINE_FLAG | TRANSPARENT75_FLAG, 0, dirty  CLIP_NUM_PAR);
			}
			else {
				display_color( foreground_image, xpos, ypos, get_owner_nr(), true, get_flag(obj_t::dirty)  CLIP_NUM_PAR);
			}
		}
		else {
			display_normal( foreground_image, xpos, ypos, 0, true, get_flag(obj_t::dirty)  CLIP_NUM_PAR);
		}
	}
}

bool roadsign_t::check_one_tran_staff_reservation(koord3d pos) const
{
	if (pos != koord3d::invalid) {
		const waytype_t wt = get_waytype()==tram_wt ? track_wt : get_waytype();
		if(schiene_t *sch = (schiene_t *)(welt->lookup(pos)->get_weg(wt))) {
			convoihandle_t reserved_convoi = sch->get_reserved_convoi();
			if (reserved_convoi.is_bound()) {
				rail_vehicle_t* rail_vehicle = (rail_vehicle_t*)reserved_convoi->front();
				if (rail_vehicle->get_working_method() == one_train_staff) {
					return true; // found
				}
			}
		}
	}
	return false;
}


#ifdef MULTI_THREAD
void roadsign_t::display_overlay(int xpos, int ypos) const
{
	if (strasse_t::show_masked_ribi) {
		const waytype_t wt = get_waytype() == tram_wt ? track_wt : get_waytype();
		if (wt == invalid_wt) { return; }
		grund_t *gr = welt->lookup(get_pos());
		weg_t *weg = gr->get_weg(wt);
		const bool is_diagonal= weg->is_diagonal();
		const uint8 way_ribi = weg->get_ribi_unmasked();

		const int raster_width = get_current_tile_raster_width();
		xpos += raster_width/2;
		ypos += raster_width/16 + tile_raster_scale_y(weg->get_yoff(), raster_width); // Must match the height of the way, not the ground

		if (is_bidirectional()) {
			if (desc->is_signal_type()) {
				const schiene_t* sch1 = (schiene_t*)weg;
				ribi_t::ribi reserved_direction = sch1->get_reserved_direction();
				display_signal_direction_rgb(xpos, ypos + raster_width / 2, raster_width, way_ribi, dir, sch1->is_reserved_directional() ? 255 : state, is_diagonal, reserved_direction, gr->get_weg_hang());
			}
			if (desc->is_private_way()) {
				uint8 state_temp = state;
				if ((1 << welt->get_active_player_nr()) & get_player_mask()) {
					// Can pass through the gate
					state_temp = roadsign_t::clear;
				}
				display_signal_direction_rgb(xpos, ypos + raster_width / 2, raster_width, way_ribi, dir, state_temp, is_diagonal, ribi_t::all, gr->get_weg_hang());
			}
			// TODO: Remove "ribi_arrow" from the intersection.
			//       Next, pass the opening direction considering the one-way restriction.
			//if (desc->is_traffic_light()) {
			//	display_signal_direction_rgb(xpos, ypos + raster_width / 2, raster_width, way_ribi, dir, clear);
			//}
		}
		else {
			if (desc->is_signal_type() || desc->is_single_way()) {
				uint8 state_temp = state;
				uint8 signal_dir = get_dir();
				uint8 open_dir = ribi_t::all;
				if (desc->get_working_method() == drive_by_sight) {
					state_temp = 254;
				}
				else if (desc->get_working_method() == one_train_staff) {
					// we need to check if the staff is there and switch the signal indication
					// one train staff display doesn't care if the next tile is reserved by another signal working method.

					// The staff post has a direction, but is capable of receiving staff from both directions.
					koord3d next_pos;
					signal_dir = ribi_t::all&~(~way_ribi);
					bool any_reserve = false;

					// Check the tile next to it. Need to be careful about the existence of the slope.
					// It can also be placed diagonally.
					if( signal_dir & ribi_t::north ) {
						koord3d next_pos = get_pos()+koord3d(0,-1,0);
						if( gr->get_weg_hang()==slope_t::south ) { is_one_high(gr->get_weg_hang()) ? next_pos.z++ : next_pos.z+=2; }
						if( check_one_tran_staff_reservation( next_pos ) ) {
							open_dir&=~ribi_t::north;
							any_reserve = true;
						}
					}
					if( signal_dir & ribi_t::south ) {
						koord3d next_pos = get_pos()+koord3d(0,1,0);
						if( gr->get_weg_hang()==slope_t::north ) { is_one_high(gr->get_weg_hang()) ? next_pos.z++ : next_pos.z+=2; }
						if( check_one_tran_staff_reservation( next_pos ) ) {
							open_dir&=~ribi_t::south;
							any_reserve = true;
						}
					}
					if( signal_dir & ribi_t::west ) {
						koord3d next_pos = get_pos()+koord3d(-1,0,0);
						if( gr->get_weg_hang()==slope_t::east ) { is_one_high(gr->get_weg_hang()) ? next_pos.z++ : next_pos.z+=2; }
						if( check_one_tran_staff_reservation( next_pos ) ) {
							open_dir&=~ribi_t::west;
							any_reserve = true;
						}
					}
					if( signal_dir & ribi_t::east ) {
						koord3d next_pos = get_pos()+koord3d(1,0,0);
						if( gr->get_weg_hang()==slope_t::west ) { is_one_high(gr->get_weg_hang()) ? next_pos.z++ : next_pos.z+=2; }
						if( check_one_tran_staff_reservation( next_pos ) ) {
							open_dir&=~ribi_t::east;
							any_reserve = true;
						}
					}

					if (!any_reserve) {
						// The staff is here
						if (state != call_on) {
							state_temp = roadsign_t::caution;
						}
					}
					else {
						// The staff has been taken by a train.
						// Entering the block is not allowed. Only exiting the block is allowed.
						// It meand the direction is limited, and when the staff is returned, it switches to drive_by_sight mode.
						state_temp = 254; // drive_by_sight
					}
					/* -- This is the end of the process for one train staff -- */
				}
				else if (desc->is_single_way()) {
					state_temp = 255;
				}

				// signal, no_entry/one_way sign
				display_signal_direction_rgb(xpos, ypos + raster_width / 2, raster_width, way_ribi, signal_dir, state_temp, is_diagonal, open_dir, gr->get_weg_hang());
			}
		}
	}
}
#endif // MULTI_THREAD


void roadsign_t::rdwr(loadsave_t *file)
{
	xml_tag_t r( file, "roadsign_t" );

	obj_t::rdwr(file);

	uint8 dummy=0;
	if(  file->get_extended_version() >= 14  ) {
		file->rdwr_byte(lane_affinity);
	}
	else {
		lane_affinity = 4; // not applied
	}
	if(  file->is_version_less(102, 3)  ) {
		file->rdwr_byte(dummy);
		if(  file->is_loading()  ) {
			ticks_ns = ticks_ow = 16;
		}
	}
	else {
		file->rdwr_byte(ticks_ns);
		file->rdwr_byte(ticks_ow);
	}
	if(  file->is_version_atleast(110, 7) || file->get_extended_version() >= 10  ) {
		file->rdwr_byte(ticks_offset);
	}
	else {
		if(  file->is_loading()  ) {
			ticks_offset = 0;
		}
	}

	if( (file->get_extended_version() == 13 && file->get_extended_revision() >= 6)
		|| (file->get_extended_version() == 14 && file->get_extended_revision() < 32) ) {
		file->rdwr_byte(dummy);
	}

	if( file->is_version_ex_atleast(14,40) ) {
	  file->rdwr_byte(ticks_amber_ns);
	  file->rdwr_byte(ticks_amber_ow);
	}
	else {
	  if( file->is_loading() ){
	    ticks_amber_ns = ticks_amber_ow = 2;
	  }
	}

	dummy = state;
	file->rdwr_byte(dummy);
	state = dummy;
	dummy = dir;
	file->rdwr_byte(dummy);
	dir = dummy;
	if(file->is_version_less(89, 0)) {
		dir = ribi_t::backward(dir);
	}

	if(file->is_saving()) {
		const char *s = desc->get_name();
		file->rdwr_str(s);
	}
	else {
		char bname[128];
		file->rdwr_str(bname, lengthof(bname));

		desc = roadsign_t::table.get(bname);
		if(desc==NULL) {
			desc = roadsign_t::table.get(translator::compatibility_name(bname));
			if(  desc==NULL  ) {
				dbg->warning("roadsign_t::rwdr", "description %s for roadsign/signal at %d,%d not found! (may be ignored)", bname, get_pos().x, get_pos().y);
				welt->add_missing_paks( bname, karte_t::MISSING_SIGN );
			}
			else {
				dbg->warning("roadsign_t::rwdr", "roadsign/signal %s at %d,%d replaced by %s", bname, get_pos().x, get_pos().y, desc->get_name() );
			}
		}
		// init ownership of private ways signs
		if(  file->is_version_less(110, 7)  &&  desc  &&  desc->is_private_way()  ) {
			ticks_ns = 0xFD;
			ticks_ow = 0xFF;
		}
	}

	if(desc && desc->is_signal_type() && file->is_saving())
	{
		signal_t* sig = (signal_t*)this;
		sig->rdwr_signal(file);
	}
}


void roadsign_t::cleanup(player_t *player)
{
	player_t::book_construction_costs(player, -desc->get_value(), get_pos().get_2d(), get_waytype());
}


void roadsign_t::finish_rd()
{
	grund_t *gr=welt->lookup(get_pos());
	if(  gr==NULL  ||  !gr->hat_weg(desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt)  ) {
		dbg->error("roadsign_t::finish_rd","roadsign: way/ground missing at %i,%i => ignore", get_pos().x, get_pos().y );
	}
	else {
		// after loading restore directions
		set_dir(dir);
		gr->get_weg(desc->get_wtyp()!=tram_wt ? desc->get_wtyp() : track_wt)->count_sign();
	}
}


// to sort compare_roadsign_desc for always the same menu order
static bool compare_roadsign_desc(const roadsign_desc_t* a, const roadsign_desc_t* b)
{
	int diff = a->get_wtyp() - b->get_wtyp();
	if (diff == 0) {
		if(a->is_choose_sign()) {
			diff += 120;
		}
		if(b->is_choose_sign()) {
			diff -= 120;
		}
		diff += (int)((uint32)a->get_flags() & ~(uint32)roadsign_desc_t::SIGN_SIGNAL) - (int)((uint32)b->get_flags()  & ~(uint32)roadsign_desc_t::SIGN_SIGNAL);
	}
	if (diff == 0) {
		/* Some type: sort by name */
		diff = strcmp(a->get_name(), b->get_name());
	}
	return diff < 0;
}


/* static stuff from here on ... */
bool roadsign_t::successfully_loaded()
{
	if(table.empty()) {
		DBG_MESSAGE("roadsign_t", "No signs found - feature disabled");
	}

	for (auto const &i : table) {
		roadsign_t::list.append(i.value);
	}

	return true;
}


bool roadsign_t::register_desc(roadsign_desc_t *desc)
{
	// avoid duplicates with same name
	if(const roadsign_desc_t *old_desc = table.remove(desc->get_name())) {
		dbg->doubled( "roadsign", desc->get_name() );
		tool_t::general_tool.remove( old_desc->get_builder() );
		delete old_desc->get_builder();
		delete old_desc;
	}

	if(  desc->get_cursor()->get_image_id(1)!=IMG_EMPTY  ) {
		// add the tool
		tool_build_roadsign_t *tool = new tool_build_roadsign_t();
		tool->set_icon( desc->get_cursor()->get_image_id(1) );
		tool->cursor = desc->get_cursor()->get_image_id(0);
		tool->set_default_param(desc->get_name());
		tool_t::general_tool.append( tool );
		desc->set_builder( tool );
	}
	else {
		desc->set_builder( NULL );
	}

	roadsign_t::table.put(desc->get_name(), desc);

	if(  desc->get_wtyp()==track_wt  &&  desc->get_flags()==roadsign_desc_t::SIGN_SIGNAL  ) {
		default_signal = desc;
	}

	return true;
}


/**
 * Fill menu with icons of given signals/roadsings from the list
 */
void roadsign_t::fill_menu(tool_selector_t *tool_selector, waytype_t wtyp, sint16 /*sound_ok*/)
{
	// check if scenario forbids this
	if (!welt->get_scenario()->is_tool_allowed(welt->get_active_player(), TOOL_BUILD_ROADSIGN | GENERAL_TOOL, wtyp)) {
		return;
	}

	const uint16 time = welt->get_timeline_year_month();

	vector_tpl<const roadsign_desc_t *>matching;

	for(auto const& i : table) {
		roadsign_desc_t const* const desc = i.value;

		bool allowed_given_current_signalbox;
		uint32 signal_group = desc->get_signal_group();

		if(signal_group)
		{
			player_t* player = welt->get_active_player();
			if(player)
			{
				const signalbox_t* sb = player->get_selected_signalbox();
				if(!sb)
				{
					allowed_given_current_signalbox = false;
				}
				else
				{
					allowed_given_current_signalbox = sb->can_add_signal(desc);
				}
			}
			else
			{
				allowed_given_current_signalbox = false;
			}
		}
		else
		{
			allowed_given_current_signalbox = true;
		}

		bool allowed_given_underground_state = true;
		if(grund_t::underground_mode == grund_t::ugm_none)
		{
			allowed_given_underground_state = desc->get_allow_underground() == 0 || desc->get_allow_underground() == 2;
		}

		if(grund_t::underground_mode == grund_t::ugm_all)
		{
			allowed_given_underground_state = desc->get_allow_underground() == 1 || desc->get_allow_underground() == 2;
		}

		if(desc->is_available(time) && desc->get_wtyp() == wtyp && desc->get_builder() && allowed_given_current_signalbox && allowed_given_underground_state)
		{
			// only add items with a cursor
			matching.insert_ordered( desc, compare_roadsign_desc );
		}
	}
	for(roadsign_desc_t const* const i : matching) {
		tool_selector->add_tool_selector(i->get_builder());
	}
}


/**
 * Finds a matching roadsign
 */
const roadsign_desc_t *roadsign_t::roadsign_search(roadsign_desc_t::types const flag, waytype_t const wt, uint16 const time)
{
	for(auto const& i : table) {
		roadsign_desc_t const* const desc = i.value;
		if(  desc->is_available(time)  &&  desc->get_wtyp()==wt  &&  desc->get_flags()==flag  ) {
			return desc;
		}
	}
	return NULL;
}

const roadsign_desc_t* roadsign_t::find_best_upgrade(bool underground)
{
	const uint16 time = welt->get_timeline_year_month();
	const roadsign_desc_t* best_candidate = NULL;

	for(auto const& i : table)
	{
		roadsign_desc_t const* const new_roadsign_type = i.value;
		if(new_roadsign_type->is_available(time)
			&& new_roadsign_type->get_upgrade_group() == desc->get_upgrade_group()
			&& new_roadsign_type->get_wtyp() == desc->get_wtyp()
			&& new_roadsign_type->get_flags() == desc->get_flags()
			&& (new_roadsign_type->get_working_method() == desc->get_working_method() || (new_roadsign_type->get_working_method() == cab_signalling && desc->get_working_method() == track_circuit_block) || (new_roadsign_type->get_working_method() == track_circuit_block && desc->get_working_method() == cab_signalling))
			&& (new_roadsign_type->get_allow_underground() == 2
			|| (underground && new_roadsign_type->get_allow_underground() == 1)
			|| (!underground && new_roadsign_type->get_allow_underground() == 0)))
		{
			if(best_candidate)
			{
				// Find the most recent current replacement signal/sign
				if(new_roadsign_type->get_intro_year_month() > best_candidate->get_intro_year_month())
				{
					best_candidate = new_roadsign_type;
				}
			}
			else
			{
				best_candidate = new_roadsign_type;
			}
		}
	}

	return best_candidate;
}

 void roadsign_t::set_scale(uint16 scale_factor)
{
	// Called from the world's set_scale method so as to avoid having to export the internal data structures of this class.
	for(roadsign_desc_t* sign : list)
	{
		sign->set_scale(scale_factor);
	}
}

 bool roadsign_t::upgrade(const roadsign_desc_t* new_desc)
 {
	 if(!new_desc)
	 {
		 return false;
	 }

	 if(desc->get_upgrade_group() == 0)
	 {
		 // Can only upgrade if an upgrade group is defined.
		 return false;
	 }

	 const roadsign_desc_t* old_desc = desc;
	 if(old_desc->get_upgrade_group() != new_desc->get_upgrade_group())
	 {
		 return false;
	 }

	 if(!get_owner()->can_afford(new_desc->get_way_only_cost()))
	 {
		 return false;
	 }

	 sint32 diff = new_desc->get_maintenance() - old_desc->get_maintenance();
	 player_t::add_maintenance(get_owner(), diff, get_waytype());
	 player_t::book_construction_costs(get_owner(), new_desc->get_way_only_cost(), get_pos().get_2d(), get_waytype());

	 desc = new_desc;
	 welt->set_dirty();
	 return true;
 }

 const koord3d roadsign_t::get_intersection() const {
	 grund_t* current_gr = welt->lookup(get_pos());
	 strasse_t* first_str = (strasse_t*)current_gr->get_weg(road_wt);
	 ribi_t::ribi current_ribi = ribi_t::none;
	 if(  !first_str  ) {
		 return koord3d::invalid;
	 } else {
		 current_ribi = first_str->get_ribi_unmasked()&(~dir);
	 }
	 for(int step = 0; step < 500; step++) {
		 grund_t *next_gr;
		 if(  ribi_t::is_single(current_ribi)  ) {
			 if(  current_gr->get_neighbour(next_gr,road_wt,current_ribi)  ) {
				 // grund found
				 strasse_t *str = (strasse_t *)next_gr->get_weg(road_wt);
				 if(  str  &&  str->get_overtaking_mode() <= oneway_mode  ) {
					 ribi_t::ribi str_ribi = str->get_ribi_unmasked();
					 if(  str_ribi == ribi_t::all  ||  ribi_t::is_threeway(str_ribi)  ) {
						 // This point is a crossing!
						 return next_gr->get_pos();
					 }
					 else {
						 // go forward
						 current_gr = next_gr;
						 current_ribi = ~((~str_ribi)|ribi_t::reverse_single(current_ribi));
					 }
				 }
				 else {
					 // there is no street or the street is not one-way road.
					 return koord3d::invalid;
				 }
			 }
			 else {
				 // grund not found
				 return koord3d::invalid;
			 }
		 }
	 }
	 return koord3d::invalid;
 }
