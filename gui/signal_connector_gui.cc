/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "signal_connector_gui.h"
#include "../display/viewport.h"
#include "../descriptor/building_desc.h"
#include "simwin.h"


gui_signalbox_changer_t::gui_signalbox_changer_t(signalbox_t* to, signal_t* from)
{
	sig = from;
	sb = to;

	set_table_layout(4,1);
	set_alignment(ALIGN_LEFT | ALIGN_TOP);
	bt_goto_signalbox.init(button_t::posbutton_automatic, NULL);
	bt_goto_signalbox.set_tooltip(translator::translate("goto_signalbox"));
	bt_goto_signalbox.set_targetpos(sb->get_pos().get_2d());
	bt_goto_signalbox.add_listener(this);
	add_component(&bt_goto_signalbox);
	bt_connect.init(button_t::square_state, translator::translate(sb->get_name()));
	bt_connect.add_listener(this);
	add_component(&bt_connect);
	gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
	lb->buf().append(sb->get_pos().get_fullstr());
	lb->update();
	add_component(&label);

	update();
}

void gui_signalbox_changer_t::update()
{
	connected = sb->get_signal_list().is_contained(sig->get_pos());
	bt_connect.pressed=connected;

	bt_connect.text_color = connected ? SYSCOL_TEXT_HIGHLIGHT : SYSCOL_TEXT;
	label.buf().printf(" %3d/%3d",
		sb->get_number_of_signals_controlled_from_this_box(), sb->get_capacity() );
	label.set_color(sb->get_number_of_signals_controlled_from_this_box()==sb->get_capacity() ? COL_WARNING : SYSCOL_TEXT);
	label.update();
}

void gui_signalbox_changer_t::draw(scr_coord offset)
{
	if(sig && sb){
		if (connected != sb->get_signal_list().is_contained(sig->get_pos())) {
			update();
		}

		set_size(get_min_size());
		gui_aligned_container_t::draw(offset);
	}
}

bool gui_signalbox_changer_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( sig->get_owner() != world()->get_active_player() ) {
		return false;
	}
	if (comp == &bt_connect) {
		koord3d old_sb_pos = sig->get_signalbox();
		const grund_t* gr = world()->lookup(old_sb_pos);
		if (gr) {
			gebaeude_t* gb = gr->get_building();
			if (gb && gb->get_tile()->get_desc()->is_signalbox()) {
				signalbox_t *old_sb = (signalbox_t*)gb;
				sb->transfer_signal(sig, old_sb);
				update();
				return true;
			}
		}
	}
	return false;
}


signal_connector_gui_t::signal_connector_gui_t(signal_t *s) :
	gui_frame_t( translator::translate("Signal connector") )
{
	sig_pos = s->get_pos();
	set_table_layout(1,0);

	update(s);

	set_windowsize(get_min_windowsize());
}


void signal_connector_gui_t::build_list(signal_t* sig)
{
	sb_selections.clear();
	const player_t *player = sig->get_owner();
	FOR(slist_tpl<signalbox_t*>, const sigb, signalbox_t::all_signalboxes) {
		if(sigb->get_owner() == player && sigb->get_first_tile() == sigb && sigb->can_add_signal(sig) ) {
			gui_signalbox_changer_t *sel = new gui_signalbox_changer_t(sigb, sig);
			sb_selections.append(sel);
		}
	}
}


void signal_connector_gui_t::update(signal_t* sig)
{
	remove_all();

	if (sig) {
		build_list(sig);
		// this signal
		add_table(2,1);
		{
			new_component<gui_label_t>(sig->get_name());
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
			lb->buf().append(sig->get_pos().get_fullstr());
			lb->update();
		}
		end_table();
		new_component<gui_empty_t>();

		new_component<gui_label_t>("Connect to:");
		if (!sb_selections.get_count()) {
			new_component<gui_label_t>("keine");
		}
		else {
			FOR(slist_tpl<gui_signalbox_changer_t *>, &selection, sb_selections) {
				add_component(selection);
			}
		}
		reset_min_windowsize();
	}
}

signal_connector_gui_t::~signal_connector_gui_t()
{
	while (!sb_selections.empty()) {
		sb_selections.remove_first();
	}
}


bool signal_connector_gui_t::is_weltpos()
{
	return ( world()->get_viewport()->is_on_center(sig_pos) );
}

void signal_connector_gui_t::map_rotate90(sint16 new_ysize)
{
	sig_pos.rotate90(new_ysize);
	signal_t* sig = welt->lookup(sig_pos)->find<signal_t>();
	update(sig);
}

void signal_connector_gui_t::draw(scr_coord pos, scr_size size)
{
	signal_t* sig = welt->lookup(sig_pos)->find<signal_t>();
	if (!sig) {
		destroy_win(this);
	}

	gui_frame_t::draw(pos, size);
}

