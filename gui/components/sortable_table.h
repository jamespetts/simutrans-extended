/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_SORTABLE_TABLE_H
#define GUI_COMPONENTS_SORTABLE_TABLE_H


#include "gui_container.h"
#include "gui_scrolled_list.h"
#include "gui_chart.h"

#include "../../tpl/vector_tpl.h"

#include "../../utils/cbuffer_t.h"



#define L_CELL_PADDING (LINESPACE>>2)

class table_cell_item_t : virtual public gui_component_t
{
public:
	enum {
		cell_no_sorting = 0,
		cell_value      = 1,
		cell_values     = 2,
		cell_text       = 3,
		cell_coord      = 4
	};

	enum align_t {
		left,
		centered,
		right
	};

protected:
	scr_size min_size;
	align_t align;
	scr_coord draw_offset;
	bool highlight=false;

public:
	table_cell_item_t(align_t align_=left) { align = align_; }

	scr_coord_val get_min_width() { return min_size.w; }
	virtual void set_width(scr_coord_val new_width) OVERRIDE;
	void set_size(scr_size size) OVERRIDE;

	// for filter
	void set_highlight(bool yesno){ highlight=yesno; }

	scr_coord_val check_height(scr_coord_val new_height);

	virtual const uint8 get_type() const { return cell_no_sorting; }

	bool is_focusable() OVERRIDE { return false; }

	void draw(scr_coord offset) OVERRIDE;
};


class text_cell_t : public table_cell_item_t
{
	const char* text; // only for direct access of non-translatable things. Do not use!
	PIXVAL color;
	bool underlined;
	// bool is_bold;

public:
	text_cell_t(const char* text = NULL, PIXVAL color = SYSCOL_TEXT, align_t align = left, bool underlined = false);

	const uint8 get_type() const OVERRIDE { return cell_text; }

	char const* get_text() const { return text; }

	void draw(scr_coord offset) OVERRIDE;
};


// Hold the coordinates and click a cell to jump to the coordinates.
class coord_cell_t : public text_cell_t
{
	koord coord;

public:
	coord_cell_t(const char* text = NULL, koord coord=koord::invalid, PIXVAL color = SYSCOL_TEXT, align_t align = left);

	koord get_coord() const { return coord; }

	const uint8 get_type() const OVERRIDE { return cell_coord; }
};


// The cell holds a single value for sorting
// Can specify a suffix.
class value_cell_t : public table_cell_item_t
{
	gui_chart_t::chart_suffix_t suffix;
protected:
	cbuffer_t buf;
	PIXVAL color;
	// bool is_bold;
	sint64 value;

public:
	value_cell_t(sint64 value, gui_chart_t::chart_suffix_t suffix= gui_chart_t::STANDARD, align_t align = left, PIXVAL color = SYSCOL_TEXT);

	// When resetting the value, care must be taken not to increase the column width.
	void set_value(sint64 value);

	sint64 get_value() const { return value; }

	const uint8 get_type() const OVERRIDE { return cell_value; }

	void draw(scr_coord offset) OVERRIDE;
};

// The cell holds two values for sorting
// There is no suffix
class values_cell_t : public table_cell_item_t
{
protected:
	cbuffer_t buf, sub_buf;
	PIXVAL color;
	sint64 value, sub_value;
	scr_coord_val sub_draw_offset_x = 0;
	bool single_line;

public:
	// single_line: Displaying two values ​​on the same line
	values_cell_t(sint64 value, sint64 sub_value, PIXVAL color = SYSCOL_TEXT, bool single_line=false);

	void set_width(scr_coord_val new_width) OVERRIDE;

	// When resetting the value, care must be taken not to increase the column width.
	void set_values(sint64 value, sint64 sub_value=0);

	sint64 get_value() const { return value; }
	sint64 get_sub_value() const { return sub_value; }

	const uint8 get_type() const OVERRIDE { return cell_values; }

	void draw(scr_coord offset) OVERRIDE;
};


class gui_sort_table_row_t : public gui_container_t, public gui_scrolled_list_t::scrollitem_t
{
public:
	static sint64 compare_value(const table_cell_item_t* a, const table_cell_item_t* b);
	static sint64 compare_values(const table_cell_item_t* a, const table_cell_item_t* b);
	static int compare_text(const table_cell_item_t* a, const table_cell_item_t* b);
	static int compare_coord(const table_cell_item_t* a, const table_cell_item_t* b);


protected:
	vector_tpl<table_cell_item_t*> owned_cells; ///< we take ownership of these pointers

	scr_coord_val row_height;

	slist_tpl<scr_coord_val> min_widths;

public:
	gui_sort_table_row_t();

	~gui_sort_table_row_t();

	void add_component(gui_component_t* comp) OVERRIDE;

	template<class C>
	C* new_component() { C* comp = new C(); add_component(comp); owned_cells.append(comp); return comp; }
	template<class C, class A1>
	C* new_component(const A1& a1) { C* comp = new C(a1); add_component(comp); owned_cells.append(comp); return comp; }
	template<class C, class A1, class A2>
	C* new_component(const A1& a1, const A2& a2) { C* comp = new C(a1, a2); add_component(comp); owned_cells.append(comp); return comp; }
	template<class C, class A1, class A2, class A3>
	C* new_component(const A1& a1, const A2& a2, const A3& a3) { C* comp = new C(a1, a2, a3); add_component(comp); owned_cells.append(comp); return comp; }
	template<class C, class A1, class A2, class A3, class A4>
	C* new_component(const A1& a1, const A2& a2, const A3& a3, const A4& a4) { C* comp = new C(a1, a2, a3, a4); add_component(comp); owned_cells.append(comp); return comp; }

	const char* get_text() const OVERRIDE { return "Ding"; }

	table_cell_item_t* get_element(sint32 i) const { return (i >= 0 && (uint32)i < owned_cells.get_count()) ? dynamic_cast<table_cell_item_t*>(owned_cells[i]) : NULL; }

	// highlight column
	void set_highlight(sint32 i, bool highlight=true);

	void set_col(uint col, scr_coord_val width);

	scr_coord_val get_min_width(uint col);

	static int compare(const table_cell_item_t* a, const table_cell_item_t* b);

	virtual bool infowin_event(event_t const* ev) OVERRIDE { return false; }

	bool is_focusable() OVERRIDE { return selected; }
	gui_component_t* get_focus() OVERRIDE { return this; }
	scr_coord get_focus_pos() OVERRIDE { return pos; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_size() const OVERRIDE { return scr_size(size.w, row_height); }

	scr_size get_min_size() const OVERRIDE { return get_size(); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


#endif
