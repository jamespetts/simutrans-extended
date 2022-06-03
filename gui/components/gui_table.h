/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_TABLE_H
#define GUI_COMPONENTS_GUI_TABLE_H


#include <string.h>

#include "../../simtypes.h"
#include "../../tpl/list_tpl.h"
#include "gui_action_creator.h"
#include "gui_component.h"
#include "../../simcolor.h"
#include "../gui_frame.h"

//typedef KOORD_VAL koord_x;
//typedef KOORD_VAL koord_y;
typedef scr_coord_val coordinate_t;
typedef FLAGGED_PIXVAL color_t;


/**
 *
 *
 * @since 22-MAR-2010
 * @author Bernd Gabriel
 */
class coordinates_t
{
private:
	coordinate_t x;
	coordinate_t y;
public:
	coordinates_t() { x = y = 0; }
	coordinates_t(coordinate_t x, coordinate_t y) { this->x = x; this->y = y; }
	coordinate_t get_x() const { return x; }
	coordinate_t get_y() const { return y; }
	void set_x(coordinate_t value) { x = value; }
	void set_y(coordinate_t value) { y = value; }
	bool equals(const coordinates_t &value) const { return x == value.x && y == value.y; }
	bool operator == (const coordinates_t &value) const { return equals(value); }
	bool operator != (const coordinates_t &value) const { return !equals(value); }
};


class gui_table_t;
class gui_table_row_t;
class gui_table_column_t;


/**
 *
 *
 * @since 05-APR-2010
 * @author Bernd Gabriel
 */
class gui_table_intercept_t
{
private:
	char *name;
	scr_coord_val size;
	bool sort_descendingly;
protected:
	scr_coord_val get_size() const { return size; }
	void set_size(scr_coord_val value) { size = value; }
public:
	gui_table_intercept_t(char *name, scr_coord_val size, bool sort_descendingly) : name(name), size(size), sort_descendingly(sort_descendingly) {}
	virtual ~gui_table_intercept_t() {}
	const char *get_name() const { return name; }
	void set_name(const char *value) { if (name) free(name); name = NULL; if (value) name = strdup(value); }
	bool get_sort_descendingly() { return sort_descendingly; }
	void set_sort_descendingly(bool value) { sort_descendingly = value; }
};


/**
 *
 *
 * @since 22-MAR-2010
 * @author Bernd Gabriel
 */
class gui_table_column_t : public gui_table_intercept_t
{
public:
	gui_table_column_t() : gui_table_intercept_t(NULL, 99, false) {}
	gui_table_column_t(scr_coord_val width) : gui_table_intercept_t(NULL, width, false) {}
	virtual int compare_rows(const gui_table_row_t &row1, const gui_table_row_t &row2) const { return sgn((uint64)&row1 - (uint64)&row2);  }
	scr_coord_val get_width() const { return get_size(); }
	void set_width(scr_coord_val value) { set_size(value); }
};


/**
 *
 *
 * @since 22-MAR-2010
 * @author Bernd Gabriel
 */
class gui_table_row_t : public gui_table_intercept_t
{
public:
	gui_table_row_t() : gui_table_intercept_t(NULL, 14, false) {}
	gui_table_row_t(scr_coord_val height) : gui_table_intercept_t(NULL, height, false) {}
	virtual int compare_columns(const gui_table_column_t &column1, const gui_table_column_t &column2) const { return sgn((uint64)&column1 - (uint64)&column2); }
	scr_coord_val get_height() const { return get_size(); }
	void set_height(scr_coord_val value) { set_size(value); }
};


class gui_table_property_t
{
private:
	gui_table_t *owner;
public:
	gui_table_property_t() { owner = NULL; }
	gui_table_t *get_owner() const { return owner; }
	void set_owner(gui_table_t *owner) {this->owner = owner; }
};


/**
 *
 *
 * @since 22-MAR-2010
 * @author Bernd Gabriel
 */
class gui_table_column_list_t : public list_tpl<gui_table_column_t>, public gui_table_property_t {
protected:
	virtual int compare_items(const gui_table_column_t *item1, const gui_table_column_t *item2) const;
	virtual gui_table_column_t *create_item() const;
};


/**
 *
 *
 * @since 22-MAR-2010
 * @author Bernd Gabriel
 */
class gui_table_row_list_t : public list_tpl<gui_table_row_t>, public gui_table_property_t {
protected:
	virtual int compare_items(const gui_table_row_t *item1, const gui_table_row_t *item2) const;
	virtual gui_table_row_t *create_item() const;
};


/**
 * A table component.
 * - allows any number of columns and rows with individual widths and heights.
 * - allows a grid of any width.
 *
 * @since 14-MAR-2010
 * @author Bernd Gabriel
 */
class gui_table_t : public gui_component_t, public gui_action_creator_t
{
	friend class gui_table_column_list_t;
	friend class gui_table_row_list_t;
private:
	koord grid_width;
	color_t grid_color;
	bool grid_visible;
	char tooltip[200];
	scr_size default_cell_size;
	// arrays controlled by change_size() via set_size() or add_column()/remove_column()/add_row()/remove_row()
	gui_table_column_list_t columns;
	gui_table_column_list_t row_sort_column_order;
	gui_table_row_list_t rows;
	gui_table_row_list_t column_sort_row_order;
	//
	bool get_column_at(scr_coord_val x, coordinate_t &column, scr_coord_val &offset) const;
	bool get_row_at(scr_coord_val y, coordinate_t &row, scr_coord_val &offset) const;
protected:
	/**
	 * change_size() is called in set_size(), whenever the size actually changes.
	 */
	virtual void change_size(const coordinates_t &old_size, const coordinates_t &new_size);

	virtual gui_table_column_t *init_column(coordinate_t x) { (void) x; return new gui_table_column_t(get_default_column_width()); }
	virtual gui_table_row_t *init_row(coordinate_t y) { (void) y; return new gui_table_row_t(get_default_row_height()); }

	/**
	 * paint_cell() is called in paint_cells(), whenever a cell has to be painted.
	 *
	 * It has to paint cell (x,y) at position offset.
	 * The default implementation calls draw() of the component of cell (x,y), if there is one.
	 */
	virtual void paint_cell(const scr_coord& offset, coordinate_t x, coordinate_t y);

	/**
	 * paint_cells() is called in draw() after painting the grid.
	 *
	 * It has to paint the cell content.
	 * The default implementation calls paint_cell() with the correct cell offset for each cell.
	 */
	virtual void paint_cells(const scr_coord& offset);

	/**
	 * paint_grid() is called in draw() before painting the cells.
	 *
	 * The default implementation draws grid_color lines of grid_width, if the grid is set to be visible.
	 */
	virtual void paint_grid(const scr_coord& offset);
public:
	gui_table_t();
    virtual ~gui_table_t();

	/**
	 * Get/set grid size.
	 *
	 * size.x is the number of cells horizontally.
	 * size.y is the number of cells vertically.
	 */
	const coordinates_t get_grid_size() const {
		return coordinates_t(columns.get_count(), rows.get_count());
	}
	void set_grid_size(const coordinates_t &value) {
		const coordinates_t &old_size = get_grid_size();
		if (!old_size.equals(value)) {
			change_size(old_size, value);
		}
	}
	bool get_owns_columns() const { return columns.get_owns_items(); }
	bool get_owns_rows() const { return rows.get_owns_items(); }
	void set_owns_columns(bool value) { columns.set_owns_items(value); }
	void set_owns_rows(bool value) { rows.set_owns_items(value); }
	virtual coordinate_t add_column(gui_table_column_t *column);
	virtual coordinate_t add_row(gui_table_row_t *row);
	virtual void remove_column(coordinate_t x);
	virtual void remove_row(coordinate_t y);
	gui_table_column_t *get_column(coordinate_t x) { return columns[x]; }
	gui_table_row_t *get_row(coordinate_t y) const { return rows[y]; }

	/**
	 * Get/set grid width / space around cells.
	 *
	 * 0: draws no grid,
	 * 1: a 1 pixel wide line, ...
	 */
	koord get_grid_width() const { return grid_width; }
	void set_grid_width(koord value) {	grid_width = value;	}

	/**
	 * Get/set grid color.
	 */
	color_t get_grid_color() const { return grid_color; }
	void set_grid_color(color_t value) { grid_color = value; }

	/**
	 * Get/set grid visibility.
	 * if grid is not visible, grid is not painted, there is space around cells.
	 */
	bool get_grid_visible() const { return grid_visible; }
	void set_grid_visible(bool value) { grid_visible = value; }

	/**
	 * Get/set width of columns and heights of rows.
	 */
	scr_coord_val get_default_column_width() const { return default_cell_size.w; }
	scr_coord_val get_default_row_height() const { return default_cell_size.h; }
	void set_default_cell_size(const scr_size& value) { default_cell_size = value; }

	scr_coord_val get_column_width(coordinate_t x) const { return columns[x]->get_width(); }
	void    set_column_width(coordinate_t x, scr_coord_val w) { columns[x]->set_width(w); }
	scr_coord_val get_table_width() const;

	scr_coord_val get_row_height(coordinate_t y) const { return rows[y]->get_height(); }
	void    set_row_height(coordinate_t y, scr_coord_val h) { rows[y]->set_height(h); }
	scr_coord_val get_table_height() const;

	scr_size get_cell_size(coordinate_t x, coordinate_t y) const { return scr_size(get_column_width(x), get_row_height(y)); }
	scr_size get_table_size() const { return scr_size(get_table_width(), get_table_height()); }

	bool get_cell_at(scr_coord_val x, scr_coord_val y, coordinates_t &cell, scr_coord &offset) const;

	virtual bool infowin_event(const event_t *ev);

	/**
	 * Set row sort order of a column.
	 * @param x index of column, whose position in the sort order is to be set.
	 * @param prio 0: main sort column, prio > 0: sort column, if rows are the same in column prio - 1.
	 */
	void set_row_sort_column_prio(coordinate_t x, int prio = 0);
	void sort_rows();

	/**
	 * Set column sort order of a row.
	 * @param y index of row, whose position in the sort order is to be set.
	 * @param prio 0: main sort row, prio > 0: sort row, if columns are the same in row prio - 1.
	 */
	void set_column_sort_row_prio(coordinate_t y, int prio = 0);
	void sort_columns();

	/**
	 * draw() paints the table.
	 */
	virtual void draw(scr_coord offset);
};


/**
 * gui_table_t::infowin_event() notifies each event to all listeners sending a pointer to an instance of this class.
 *
 * @since 22-MAR-2010
 * @author Bernd Gabriel
 */
class gui_table_event_t
{
private:
	gui_table_t *table;
	const event_t *ev;
public:
	gui_table_event_t(gui_table_t *table, const event_t *ev) {
		this->table = table;
		this->ev = ev;
	}

	/**
	 * the table that notifies.
	 */
	gui_table_t *get_table() const { return table; }

	/**
	 * the event that the notifying table received.
	 */
	const event_t *get_event() const { return ev; }

	/**
	 * Does the mouse (ev->mx, ev->my) point to a cell?
	 * False, if mouse is outside table or mouse points to grid/space around cells.
	 * True and the cell coordinates can be found in 'cell'.
	 */
	bool is_cell_hit;

	/**
	 * Contains the coordinates of the cell the mouse points to, if is_cell_hit is true.
	 * If is_cell_hit is false cell is undefined.
	 */
	coordinates_t cell;

	/**
	 * Contains the pixel offset of the cell within the table.
	 * If is_cell_hit is false cell is undefined.
	 */
	scr_coord offset;
};

#endif
