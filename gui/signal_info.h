/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SIGNAL_INFO_H
#define GUI_SIGNAL_INFO_H


#include "obj_info.h"
#include "../obj/signal.h"
#include "components/action_listener.h"
#include "components/gui_label.h"
#include "components/gui_numberinput.h"
#include "components/gui_container.h"
#include "../player/simplay.h"


/**
 * Info window for signals
 */
class signal_info_t : public obj_infowin_t, public action_listener_t
{
private:
	const signal_t* sig;
	button_t remove;
	button_t bt_goto_signalbox;
	button_t bt_info_signalbox;
	button_t bt_switch_signalbox;

	gui_label_buf_t lb_sb_name, lb_sb_distance;

 public:
	signal_info_t(signal_t* const s);

	const char *get_help_filename() const OVERRIDE { return "signals_overview.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	// called, after external change
	void update_data();

	void map_rotate90(sint16) OVERRIDE { update_data(); }

	void draw(scr_coord pos, scr_size size) OVERRIDE;
};

#endif
