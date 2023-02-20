/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_LINELIST_STATS_T_H
#define GUI_LINELIST_STATS_T_H


#include "components/gui_component.h"
#include "components/gui_aligned_container.h"
#include "components/gui_label.h"
#include "../simline.h"

#include "../tpl/minivec_tpl.h"


// Similar function: gui_convoy_handle_catg_img_t
class gui_line_handle_catg_img_t : public gui_container_t
{
private:
	linehandle_t line;

public:
	gui_line_handle_catg_img_t(linehandle_t line);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE;
};


class gui_matching_catg_img_t : public gui_component_t
{
private:
	minivec_tpl<uint8> matching_freight_catg;

public:
	gui_matching_catg_img_t(const minivec_tpl<uint8> &goods_catg_index_a, const minivec_tpl<uint8> &goods_catg_index_b);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


// line name with line letter code
class gui_line_label_t : public gui_aligned_container_t
{
	linehandle_t line;
	cbuffer_t name_buf, tooltip_buf;
	uint16 old_seed = -1;

public:
	gui_line_label_t(linehandle_t line);

	void update_check();
	void update();

	void draw(scr_coord offset) OVERRIDE;
};


#endif
