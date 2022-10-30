/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_LINE_NETWORK_H
#define GUI_COMPONENTS_GUI_LINE_NETWORK_H


#include "gui_aligned_container.h"
#include "gui_component.h"
#include "gui_combobox.h"
#include "gui_action_creator.h"
#include "gui_button.h"
#include "gui_label.h"
#include "../simwin.h"

#include "../../simhalt.h"
#include "../../simline.h"
#include "../../convoihandle_t.h"

#include "../../tpl/slist_tpl.h"

#define L_CONNECTION_LINES_WIDTH (LINEASCENT*8)

struct connection_line_t
{
	PIXVAL color;
	bool is_own;    // flag for line style
	bool is_convoy; // false means line. flag for line width
};

class gui_connection_lines_t : public gui_component_t
{
	slist_tpl<connection_line_t> lines;

public:
	gui_connection_lines_t();

	void append_line(connection_line_t line) {
		lines.append(line);
		size.w = max(lines.get_count()*5, L_CONNECTION_LINES_WIDTH);
	};
	void clear() { lines.clear(); };

	void set_height(scr_coord_val h) { size.h = h; };

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return scr_size(L_CONNECTION_LINES_WIDTH, D_ENTRY_NO_HEIGHT); }
	scr_size get_max_size() const OVERRIDE { return scr_size(get_min_size().w, scr_size::inf.h); }
};

// Accessable line information panel
class gui_transfer_line_t : public gui_aligned_container_t, private action_listener_t
{
	linehandle_t line;
	convoihandle_t cnv;
	button_t bt_access_minimap/*, bt_access_info*/;
	koord transfer_pos;

	gui_label_buf_t lb_service_frequency;

public:
	gui_transfer_line_t(linehandle_t line, koord halt_pos);
	gui_transfer_line_t(convoihandle_t cnv, koord halt_pos);

	void init_table();

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


class gui_line_transfer_guide_t : public gui_aligned_container_t
{
private:
	halthandle_t halt;
	linehandle_t line;
	convoihandle_t cnv;
	bool filter_same_company;
	uint8 filter_catg;

	uint32 seed=0;

	gui_connection_lines_t cont_lines;

	void update();

public:
	gui_line_transfer_guide_t(halthandle_t transfer_halt, linehandle_t line, bool filter_same_company, uint8 filter_catg=goods_manager_t::INDEX_NONE);
	gui_line_transfer_guide_t(halthandle_t transfer_halt, convoihandle_t cnv, bool filter_same_company, uint8 filter_catg = goods_manager_t::INDEX_NONE);

	void set_halt(halthandle_t h) { this->halt = h; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_line_network_t : public gui_aligned_container_t, private action_listener_t
{
private:
	linehandle_t line;
	convoihandle_t cnv;

	gui_combobox_t filter_goods_catg;
	bool filter_own_network=true;
	button_t bt_own_network;
	uint8 filter_catg=goods_manager_t::INDEX_NONE;

public:
	gui_line_network_t(linehandle_t line);
	gui_line_network_t(convoihandle_t cnv);

	void set_line(linehandle_t line_) { line = line_; cnv=convoihandle_t(); init_table(); }
	void set_convoy(convoihandle_t cnv_) { line=linehandle_t(); cnv=cnv_; init_table(); }

	void init_table();

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t v) OVERRIDE;
};

#endif
