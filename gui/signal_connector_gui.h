/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SIGNAL_CONNECTOR_GUI_H
#define GUI_SIGNAL_CONNECTOR_GUI_H


#include "gui_frame.h"
#include "components/gui_label.h"
#include "components/gui_button.h"
#include "components/action_listener.h"
#include "../obj/signal.h"
#include "../simsignalbox.h"


class gui_signalbox_changer_t : public gui_aligned_container_t, private action_listener_t
{
	const signal_t* sig;
	signalbox_t* sb;

	bool connected=false;

	bool transfer_signal(signal_t* s, signalbox_t* sb);

	gui_label_buf_t label;
	button_t bt_connect, bt_goto_signalbox;

	void update();

public:
	gui_signalbox_changer_t(signalbox_t* to, const signal_t *from);

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};

/*
 * Dialogue to increase map size.
 */
class signal_connector_gui_t : public gui_frame_t
{
private:
	slist_tpl<gui_signalbox_changer_t *>sb_selections;

	//signal_t* sig; // Removal cannot be processed in this case
	koord3d sig_pos;

	// update signal box list
	void build_list(const signal_t* sig);

	// update table
	void update(const signal_t* sig);

public:
	signal_connector_gui_t(const signal_t *s);

	~signal_connector_gui_t();

	//const char * get_help_filename() const OVERRIDE { return "signal_connector.txt";}

	koord3d get_weltpos(bool) OVERRIDE { return sig_pos; }
	bool is_weltpos() OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void map_rotate90(sint16) OVERRIDE;
};

#endif
