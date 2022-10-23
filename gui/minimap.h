/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_MINIMAP_H
#define GUI_MINIMAP_H


#include "components/gui_component.h"
#include "../halthandle_t.h"
#include "../simline.h"
#include "../convoihandle_t.h"
#include "../dataobj/schedule.h"
#include "../tpl/array2d_tpl.h"
#include "../tpl/vector_tpl.h"


class karte_ptr_t;
class fabrik_t;
class grund_t;
class stadt_t;
class player_t;
class schedule_t;
class loadsave_t;
class goods_desc_t;

#define MAX_SEVERITY_COLORS 10


/**
 * This class is used to render the actual minimap.
 * Implemented as singleton.
 */
class minimap_t : public gui_component_t
{
public:
	enum MAP_DISPLAY_MODE: unsigned int {
		PLAIN                       = 0,
		MAP_TOWN                    = 1u << 0u,
		MAP_STATION_COVERAGE        = 1u << 1u,
		MAP_CONVOYS                 = 1u << 2u,
		MAP_FREIGHT                 = 1u << 3u,
		MAP_MAIL_WAITING            = 1u << 4u,
		MAP_SERVICE                 = 1u << 5u,
		MAP_TRAFFIC                 = 1u << 6u,
		MAP_ORIGIN                  = 1u << 7u,
		MAP_TRANSFER                = 1u << 8u,
		MAP_PAX_WAITING             = 1u << 9u,
		MAP_TRACKS                  = 1u << 10u,
		MAX_SPEEDLIMIT              = 1u << 11u,
		MAP_POWERLINES              = 1u << 12u,
		MAP_TOURIST                 = 1u << 13u,
		MAP_FACTORIES               = 1u << 14u,
		MAP_DEPOT                   = 1u << 15u,
		MAP_FOREST                  = 1u << 16u,
		MAP_CITYLIMIT               = 1u << 17u,
		MAP_PAX_DEST                = 1u << 18u,
		MAP_OWNER                   = 1u << 19u,
		MAP_LINES                   = 1u << 20u,
		MAP_LEVEL                   = 1u << 21u,
		MAP_GOODS_WAITING           = 1u << 22u,
		MAP_CONDITION               = 1u << 23u,
		MAP_WEIGHTLIMIT             = 1u << 24u,
		MAP_ACCESSIBILITY_COMMUTING = 1u << 25u,
		MAP_ACCESSIBILITY_TRIP      = 1u << 26u,
		MAP_STAFF_FULFILLMENT       = 1u << 27u,
		MAP_MAIL_DELIVERY           = 1u << 28u,
		MAP_CONGESTION              = 1u << 29u,
		MAP_MAIL_HANDLING_VOLUME    = 1u << 30u,
		MAP_GOODS_HANDLING_VOLUME   = 1u << 31u,

		MAP_MODE_HALT_FLAGS = (MAP_SERVICE|MAP_ORIGIN|MAP_TRANSFER|MAP_PAX_WAITING|MAP_MAIL_WAITING|MAP_GOODS_WAITING|MAP_MAIL_HANDLING_VOLUME|MAP_GOODS_HANDLING_VOLUME),
		MAP_MODE_FLAGS = (MAP_TOWN|MAP_CITYLIMIT|MAP_SERVICE|MAP_PAX_WAITING|MAP_MAIL_WAITING|MAP_GOODS_WAITING|MAP_TRANSFER|MAP_LINES|MAP_FACTORIES|MAP_ORIGIN|MAP_DEPOT|MAP_TOURIST|MAP_CONVOYS|MAP_MAIL_HANDLING_VOLUME|MAP_GOODS_HANDLING_VOLUME)
	};

private:
	//This one really has to be static
	static minimap_t *single_instance;
	static karte_ptr_t world;

	minimap_t();



	/// the terrain map
	array2d_tpl<PIXVAL> *map_data{nullptr};

	/// nonstatic, if we have someday many maps ...
	void set_map_color_clip( sint16 x, sint16 y, PIXVAL color );

	/// all stuff connected with schedule display
	class line_segment_t
	{
	public:
		koord start, end;
		schedule_t *schedule;
		player_t *player;
		waytype_t    wtyp;
		uint8 colorcount;
		uint8 line_color_index;
		uint8 start_offset;
		uint8 end_offset;
		bool start_diagonal;

		line_segment_t() {}
		line_segment_t( koord s, uint8 so, koord e, uint8 eo, schedule_t *f, player_t *p, uint8 cc, bool diagonal, uint8 lc_idx=255 ) {
			schedule = f;
			wtyp = f->get_waytype();
			player = p;
			colorcount = cc;
			start_diagonal = diagonal;
			line_color_index = lc_idx;
			if(  s.x<e.x  ||  (s.x==e.x  &&  s.y<e.y)  ) {
				start = s;
				end = e;
				start_offset = so;
				end_offset = eo;
			}
			else {
				start = e;
				end = s;
				start_offset = eo;
				end_offset = so;
			}
		}

		bool operator==(const line_segment_t & k) const;
	};

	/// Ordering based on first start then end coordinate
	class LineSegmentOrdering
	{
	public:
		bool operator()(const minimap_t::line_segment_t& a, const minimap_t::line_segment_t& b) const;
	};

	vector_tpl<line_segment_t> schedule_cache;
	convoihandle_t current_cnv;
	halthandle_t current_halt;
	uint8 last_schedule_counter{};
	vector_tpl<halthandle_t> stop_cache;

	/// adds a schedule to cache
	void add_to_schedule_cache( convoihandle_t cnv, bool with_waypoints );

	/**
	 * //TODO Why are the following two static?
	 * 0: normal
	 * everything else: special map
	 */
	MAP_DISPLAY_MODE mode{MAP_TOWN};
	MAP_DISPLAY_MODE last_mode{MAP_TOWN};
	static const uint8 severity_color[MAX_SEVERITY_COLORS];

	koord screen_to_map_coord(const scr_coord&) const;

	/// for passenger destination display
	const stadt_t *selected_city{nullptr};
	uint32 pax_destinations_last_change{};

	koord last_world_pos;

	/**
	 * current and new offset and size (to avoid drawing invisible parts)
	 *
	 * gui_component_t::size is always equal to max_size.
	 *
	 * These are size and offset of visible part of map.
	 * We only show and compute this.
	 */
	scr_coord cur_off{scr_coord(0,0)};
	scr_coord new_off{scr_coord(0,0)};
	scr_size cur_size{scr_size(0,0)};
	scr_size new_size{scr_size(0,0)};

	/// true, if full redraw is needed
	bool needs_redraw{true};

	const fabrik_t* get_factory_near(koord pos, bool large_area) const;

	const fabrik_t* draw_factory_connections(const fabrik_t* const fab, bool supplier_link, const scr_coord pos) const;

	//TODO why static?
	static sint32 max_cargo;
	static sint32 max_passed;

	/// the zoom factors
	sint16 zoom_out{1};
	sint16 zoom_in{1};

	/// if true, draw the map with 45 degree rotation
	bool isometric{false};

	bool is_matching_freight_catg(const minivec_tpl<uint8> &goods_catg_index);

	void set_map_color(koord k, PIXVAL color);

public:
	scr_coord map_to_screen_coord(const koord &k) const;

	void toggle_isometric() { isometric = !isometric; }
	bool is_isometric() const { return isometric; }

	bool is_visible{false};

	bool show_network_load_factor{false};
	bool show_contour{true};
	bool show_buildings{true};

	int player_showed_on_map{};
	int transport_type_showed_on_map{simline_t::line};
	const goods_desc_t *freight_type_group_index_showed_on_map{nullptr};

	/**
	 * returns a color based on an amount (high amount/scale -> color shifts from green to red)
	 */
	static PIXVAL calc_severity_color(sint32 amount, sint32 scale);

	/**
	 * returns a color based on an amount (high amount/scale -> color shifts from green to red)
	 * but using log scale
	 */
	static PIXVAL calc_severity_color_log(sint32 amount, sint32 scale);

	/**
	 * returns a color based on the current height
	 */
	static PIXVAL calc_height_color(sint16 height, sint16 groundwater);

	/// needed for town passenger map
	static PIXVAL calc_ground_color (const grund_t *gr, bool show_contour = true, bool show_buildings = true);

	static PIXVAL get_depot_color(obj_t::typ);

	/// we are single instance ...
	static minimap_t *get_instance();

	// HACK! since we cannot set cleanly the current offset/size, we use this helper function
	void set_xy_offset_size( scr_coord off, const scr_size& size ) {
		new_off = off;
		new_size = size;
	}

	/// update color with render mode (but few are ignored ... )
	void calc_map_pixel(koord k);

	void calc_map();

	/// calculates the current size of the map (but do not change anything else)
	void calc_map_size();

	~minimap_t() override;

	void init();

	void finalize();

	void set_display_mode(MAP_DISPLAY_MODE new_mode);

	MAP_DISPLAY_MODE get_display_mode() const { return mode; }

	/// updates the map (if needed)
	void new_month();

	void invalidate_map_lines_cache();

	bool infowin_event(event_t const*) OVERRIDE;

	void draw(scr_coord pos) OVERRIDE;

	void set_selected_cnv( convoihandle_t c );
	inline convoihandle_t get_selected_cnv() { return current_cnv; }
	void set_selected_halt(  halthandle_t h  );

	void set_selected_city( const stadt_t* _city );

	bool is_city_selected(const stadt_t* city) const { return selected_city == city; }

	/**
	 * @returns true if zoom factors changed
	 */
	bool change_zoom_factor(bool magnify);

	void get_zoom_factors(sint16 &zoom_out_, sint16 &zoom_in_) const {
		zoom_in_ = zoom_in;
		zoom_out_ = zoom_out;
	}

	void rdwr(loadsave_t *file);

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;

};

#endif
