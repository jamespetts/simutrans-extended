/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_SORTABLE_TABLE_HEADER_H
#define GUI_COMPONENTS_SORTABLE_TABLE_HEADER_H


#include "gui_container.h"
#include "gui_action_creator.h"

class sortable_header_cell_t : /*public gui_action_creator_t,*/ public gui_component_t
{
	const char* text;
	bool enabled=true;
	scr_coord_val min_width; // element width = without padding

public:
	sortable_header_cell_t(char const* const text_, bool enable=true);

	scr_coord_val get_min_width() { return min_width; }

	void set_width(scr_coord_val new_width) OVERRIDE;

	bool reversed = false;
	bool selected = false;
	void enable(bool yesno = true) { enabled = yesno; }
	void draw(scr_coord offset) OVERRIDE;

	//bool infowin_event(event_t const* ev) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }

};

class gui_sort_table_header_t : public gui_container_t, public gui_action_creator_t
{
	vector_tpl<sortable_header_cell_t*> owned_cells; ///< we take ownership of these pointers
	uint8 selected_col=0;

	bool reversed=false;

public:
	gui_sort_table_header_t();
	~gui_sort_table_header_t();

	void add_component(gui_component_t* comp) OVERRIDE;

	template<class C>
	C* new_component() { C* comp = new C(); add_component(comp); owned_cells.append(comp); return comp; }
	template<class C, class A1>
	C* new_component(const A1& a1) { C* comp = new C(a1); add_component(comp); owned_cells.append(comp); return comp; }

	void set_col(uint col, scr_coord_val width);

	scr_coord_val get_min_width(uint col);

	void set_selection(uint32 col);

	uint8 get_selected_column() { return selected_col;	}
	bool is_reversed() { return reversed; }

	bool infowin_event(event_t const* ev) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return get_size(); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


#endif
