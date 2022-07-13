/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CONVOY_ITEM_H
#define GUI_CONVOY_ITEM_H


#include "components/gui_scrolled_list.h"
#include "../convoihandle_t.h"

/**
 * Container for list entries - consisting of text and color
 */
class convoy_scrollitem_t : public gui_scrolled_list_t::const_text_scrollitem_t
{
private:
	convoihandle_t cnv;
	bool use_convoy_status_color;
public:
	convoy_scrollitem_t( convoihandle_t c, bool auto_text_color=true ) : gui_scrolled_list_t::const_text_scrollitem_t( NULL, SYSCOL_TEXT ) { cnv = c; use_convoy_status_color = auto_text_color; }
	PIXVAL get_color() const OVERRIDE;
	convoihandle_t get_convoy() const { return cnv; }
	char const* get_text() const OVERRIDE;
	void set_text(char const*) OVERRIDE;
	bool is_editable() const OVERRIDE { return true; }
	bool is_valid() const OVERRIDE { return cnv.is_bound(); } //  can be used to indicate invalid entries
};

#endif
