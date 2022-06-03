/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_HALT_LIST_FRAME_H
#define GUI_HALT_LIST_FRAME_H


#include "simwin.h"
#include "../simcity.h"
#include "gui_frame.h"
#include "halt_list_stats.h"
#include "components/gui_button.h"
#include "components/gui_combobox.h"
#include "components/action_listener.h"
#include "../tpl/vector_tpl.h"

class player_t;
class goods_desc_t;
class gui_scrolled_halt_list_t;

/**
 * Displays a scrollable list of all stations of a player
 */
class halt_list_frame_t : public gui_frame_t , private action_listener_t
{
public:
	enum sort_mode_t {
		nach_name,
		by_waiting_pax,
		by_waiting_mail,
		by_waiting_goods,
		nach_typ,
		by_tiles,
		by_capacity,
		by_overcrowding_rate,
		by_potential_pax,
		by_potential_mail,
		by_pax_happy_last_month,
		by_pax_handled_last_month,
		by_mail_delivered_last_month,
		by_mail_handled_last_month,
		by_goods_handled_last_month,
		by_convoy_arrivals_last_month,
		by_region,
		by_surrounding_population,
		by_surrounding_mail_demand,
		by_surrounding_visitor_demand,
		by_surrounding_jobs,
		SORT_MODES
	};

	enum filter_flag_t {
		any_filter             = 1 << 0,
		name_filter            = 1 << 1,
		typ_filter             = 1 << 2,
		spezial_filter         = 1 << 3,
		ware_ab_filter         = 1 << 4,
		ware_an_filter         = 1 << 5,

		sub_filter             = 1 << 6,
		// Start sub-filter from here!
		frachthof_filter       = 1 << 6,
		bahnhof_filter         = 1 << 7,
		bushalt_filter         = 1 << 8,
		dock_filter            = 1 << 9,
		airport_filter         = 1 << 10,
		ueberfuellt_filter     = 1 << 11,
		ohneverb_filter        = 1 << 12,
		monorailstop_filter    = 1 << 13,
		maglevstop_filter      = 1 << 14,
		narrowgaugestop_filter = 1 << 15,
		tramstop_filter        = 1 << 16
	};

private:
	player_t *m_player;

	static const char *sort_text[SORT_MODES];
	static const char *display_mode_text[halt_list_stats_t::HALTLIST_MODES];

	uint32 last_world_stops;

	/*
	 * All gui elements of this dialog:
	 */
	button_t sort_order;
	button_t filter_on;
	button_t filter_details;
	button_t btn_show_mutual_use, bt_cancel_cityfilter;
	gui_combobox_t sortedby, cb_display_mode;
	gui_scrolled_halt_list_t *scrolly;
	gui_label_buf_t lb_target_city;

	/*
	 * Child window, if open
	 */
	gui_frame_t *filter_frame;

	stadt_t *filter_city;

	/*
	 * All filter settings are static, so they are not reset each
	 * time the window closes.
	 */
	static sort_mode_t sortby;
	static bool sortreverse;

	bool show_mutual_stops = false;
	static uint32 filter_flags;

	static char name_filter_value[64];

	static slist_tpl<const goods_desc_t *> waren_filter_ab;
	static slist_tpl<const goods_desc_t *> waren_filter_an;

	/// refill the list of halt info elements
	void fill_list();

public:

	static bool compare_halts(halthandle_t, halthandle_t);
	static uint8 display_mode;

	halt_list_frame_t(stadt_t *filter_city = NULL);

	virtual ~halt_list_frame_t();

	/**
	 * The filter frame tells us when it is closed.
	 */
	void filter_frame_closed() { filter_frame = NULL; }

	// must be handled, because we could not use scrollpanel
	bool infowin_event(event_t const*) OVERRIDE;

	/**
	 * Draw new component. The values to be passed refer to the window
	 * i.e. It's the screen coordinates of the window where the
	 * component is displayed.
	 */
	void draw(scr_coord pos, scr_size size) OVERRIDE;

	/// sort & filter halts in list
	void sort_list();

	/**
	 * Set the window associated helptext
	 * @return the filename for the helptext, or NULL
	 */
	const char *get_help_filename() const OVERRIDE {return "haltlist.txt"; }

	static bool get_reverse() { return sortreverse; }
	static void set_reverse(bool reverse) { sortreverse = reverse; }

	static bool get_filter(filter_flag_t filter) { return (filter_flags & filter) != 0; }
	static void set_filter(filter_flag_t filter, bool on) { filter_flags = on ? (filter_flags | filter) : (filter_flags & ~filter); }

	static char *access_name_filter() { return name_filter_value; }

	static bool get_ware_filter_ab(const goods_desc_t *ware) { return waren_filter_ab.is_contained(ware); }
	static void set_ware_filter_ab(const goods_desc_t *ware, int mode);
	static void set_alle_ware_filter_ab(int mode);

	static bool get_ware_filter_an(const goods_desc_t *ware) { return waren_filter_an.is_contained(ware); }
	static void set_ware_filter_an(const goods_desc_t *ware, int mode);
	static void set_alle_ware_filter_an(int mode);

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void map_rotate90( sint16 ) OVERRIDE { fill_list(); }

	void rdwr(loadsave_t* file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_halt_list; }

	void set_cityfilter(stadt_t *city);
};

#endif
