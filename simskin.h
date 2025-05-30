/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMSKIN_H
#define SIMSKIN_H


#include "simcolor.h"
#include "simtypes.h"

#define SYMBOL_OVERCROWDING (skinverwaltung_t::pax_evaluation_icons ? skinverwaltung_t::pax_evaluation_icons->get_image_id(1) : IMG_EMPTY)


// For test purposes themes can be disabled or an alternative theme.tab file can be used.
//  -1 = No theme, use internal fallback
//   0 = Normal use, read theme.tab
// n>0 = Use alternative file named theme_n.tab

template<class T> class slist_tpl;
class skin_desc_t;


class skinverwaltung_t {
public:
	enum skintyp_t {
		nothing,
		menu,
		cursor,
		symbol,
		misc
	};

	/// @name icons used in the toolbars
	/// @{

	/// icon images for advanced tools (everything that involves clicking on the map)
	static const skin_desc_t *tool_icons_general;
	/// icon images for simple tools (eg pause, fast forward)
	static const skin_desc_t *tool_icons_simple;
	/// icon images for GUI tools (which open windows)
	static const skin_desc_t *tool_icons_dialoge;
	/// icon images for toolbars
	static const skin_desc_t *tool_icons_toolbars;
	/// icon to skin toolbar background
	static const skin_desc_t *toolbar_background;
	/// @}

	/**
	 * Different GUI elements
	 */
	static const skin_desc_t* button;
	static const skin_desc_t* round_button;
	static const skin_desc_t* check_button;
	static const skin_desc_t* posbutton;
	static const skin_desc_t* back;
	static const skin_desc_t* scrollbar;
	static const skin_desc_t* divider;
	static const skin_desc_t* editfield;
	static const skin_desc_t* listbox;
	static const skin_desc_t* gadget;

	/// @name pictures used in the GUI
	/// @{

	/// image shown in welcome screen @see banner.cc
	static const skin_desc_t *logosymbol;
	/// image shown when loading pakset
	static const skin_desc_t *biglogosymbol;
	/// image shown with 'happy new year' message
	static const skin_desc_t *neujahrsymbol;
	/// image shown when creating new world
	static const skin_desc_t *neueweltsymbol;
	/// image shown in language selection
	static const skin_desc_t *flaggensymbol;
	/// image shown in message boxes @see gui/messagebox.h
	static const skin_desc_t *meldungsymbol;
	/// image shown in message options window
	static const skin_desc_t *message_options;
	/// image shown in color selection window
	static const skin_desc_t *color_options;
	// isometric compass for main map (evt. minimap)
	static const skin_desc_t *compass_iso;
	// normal staight compass for minimap
	static const skin_desc_t *compass_map;
	/// @}

	/// @name icons used for the tabs in the line management window
	/// @{
	static const skin_desc_t *zughaltsymbol;
	static const skin_desc_t *autohaltsymbol;
	static const skin_desc_t *schiffshaltsymbol;
	static const skin_desc_t *airhaltsymbol;
	static const skin_desc_t *monorailhaltsymbol;
	static const skin_desc_t *maglevhaltsymbol;
	static const skin_desc_t *narrowgaugehaltsymbol;
	static const skin_desc_t *bushaltsymbol;
	static const skin_desc_t *tramhaltsymbol;
	///@}

	/// @name icons shown in status bar at the bottom of the screen
	/// @{
	static const skin_desc_t *networksymbol;
	static const skin_desc_t *timelinesymbol;
	static const skin_desc_t *fastforwardsymbol;
	static const skin_desc_t *pausesymbol;
	static const skin_desc_t *seasons_icons;
	/// @}

	/// @name icons used to show which halt serves passengers / mail / freight
	/// @{
	static const skin_desc_t *passengers;
	static const skin_desc_t *mail;
	static const skin_desc_t *goods;
	static const skin_desc_t *goods_categories;
	/// @}

	/// @name icons used to passenger/mail evaluations
	/// @{
	static const skin_desc_t *pax_evaluation_icons;
	static const skin_desc_t *mail_evaluation_icons;
	/// @}

	/// images used to alert in line with message text
	static const skin_desc_t *alerts;

	/// images shown in display of lines in mini-map
	static const skin_desc_t *station_type;

	/// image to indicate power supply of factories
	static const skin_desc_t *electricity;
	/// image to indicate that an attraction is inside a town (attraction list window)
	static const skin_desc_t *intown;
	/// image to indicate that the vehicle has upgrade target
	static const skin_desc_t *upgradable;
	/// image to indicate that the line missing scheduled slot
	static const skin_desc_t *missing_scheduled_slot;
	/// image shown in display of travel time to the destination / lead time
	static const skin_desc_t *travel_time;
	/// image for comfort value
	static const skin_desc_t *comfort;

	/// images for the factory information
	static const skin_desc_t *input_output;
	static const skin_desc_t *in_transit;
	static const skin_desc_t *ind_sector_symbol;

	/// arrows representing the reverse order of the schedule
	static const skin_desc_t *reverse_arrows;

	/// image shown in display of waiting time at the station
	static const skin_desc_t *waiting_time;
	static const skin_desc_t *service_frequency;
	/// image to indicate that the movement method is walking
	static const skin_desc_t *on_foot;

	/// image to clarify the function of buttons
	static const skin_desc_t *open_window;
	static const skin_desc_t *search;


	/// @name cursors
	/// @{

	/// cursors for tools
	static const skin_desc_t *cursor_general;
	/// for allegro: emulate mouse cursor
	static const skin_desc_t *mouse_cursor;
	/// symbol to mark tiles by AI players and text labels
	static const skin_desc_t *belegtzeiger;
	/// icon to mark start tiles for construction (ie the bulldozer image)
	static const skin_desc_t *bauigelsymbol;
	/// @}

	/// shown in hidden-buildings mode instead of buildings images
	static const skin_desc_t *construction_site;
	/// texture to be shown beneath city roads to indicate pavements
	static const skin_desc_t *fussweg;
	/// transformer image: supply
	static const skin_desc_t *pumpe;
	/// transformer image: consumer
	static const skin_desc_t *senke;
	/// texture to be shown beneath ways in tunnel
	static const skin_desc_t *tunnel_texture;

	static bool register_desc(skintyp_t type, const skin_desc_t *desc);
	static bool successfully_loaded(skintyp_t type);

	/**
	 * retrieves objects with type=menu and given name
	 * @param str pointer to beginning of name string (not null-terminated)
	 * @param len length of string
	 * @return pointer to skin object or NULL if nothing found
	 */
	static const skin_desc_t *get_extra( const char *str, int len );

	static const skin_desc_t *get_waytype_skin(const waytype_t wt);

private:
	/// holds objects from paks with type 'menu'
	static slist_tpl<const skin_desc_t *>extra_obj;
};

#endif
