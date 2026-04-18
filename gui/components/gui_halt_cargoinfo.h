/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_HALT_CARGOINFO_H
#define GUI_COMPONENTS_GUI_HALT_CARGOINFO_H


#include "gui_aligned_container.h"
#include "gui_label.h"
#include "../../simhalt.h"


class gui_halt_waiting_table_t : public gui_aligned_container_t
{
public:
	// transfer_mode: 1=transfer_in, 2=transfer_out
	gui_halt_waiting_table_t(slist_tpl<ware_t> const& warray, uint8 filter_bits, uint8 merge_condition_bits, uint32 border=0/* 0=auto */, uint8 transfer_mode=0, const schedule_t *sch=NULL);
};


/**
 * List of cargo waiting at the stop
 */
class gui_halt_cargoinfo_t : public gui_aligned_container_t
{
public:
	enum {
		SHOW_WAITING_PAX   = 1 << 0,
		SHOW_WAITING_MAIL  = 1 << 1,
		SHOW_WAITING_GOODS = 1 << 2,
		SHOW_TRANSFER_IN   = 1 << 3,
		SHOW_TRANSFER_OUT  = 1 << 4,
		SHOW_ALL_CARGOES = SHOW_WAITING_PAX | SHOW_WAITING_MAIL | SHOW_WAITING_GOODS
	};

	enum sort_mode_t {
		by_amount          = 0,
		by_via             = 1, // stop name
		by_origin          = 2, // stop name
		by_category        = 3,
		by_via_distance    = 4,
		by_origin_distance = 5,
		by_destination     = 6, // distance
		by_route           = 7, // line or convoy
		SORT_MODES
	};

	static bool sort_reverse;

private:
	halthandle_t halt;

	void sort_cargo(slist_tpl<ware_t> & warray, uint8 sort_mode);

	// returns waiting sum of this category
	uint32 list_by_catg(uint8 ft_filter_bits, uint8 merge_condition_bits, uint8 sort_mode, linehandle_t line=linehandle_t(), convoihandle_t cnv=convoihandle_t(), uint8 entry_start=0, uint8 entry_end = 255);

	// helper function of update()
	void list_by_route(uint8 filter_bits, uint8 merge_condition_bits, uint8 sort_mode, linehandle_t line, convoihandle_t cnv = convoihandle_t());

public:
	gui_halt_cargoinfo_t(halthandle_t halt);

	// for loading
	void set_halt(halthandle_t halt_) { halt = halt_; }

	scr_size get_max_size() const OVERRIDE { return scr_size(scr_size::inf.w, get_min_size().h); }

	void update(uint8 ft_filter_bits = SHOW_ALL_CARGOES, uint8 merge_condition_bits=0, uint8 sort_mode = 0/*by_amount*/, bool route_mode=false, int player_nr=-1, uint16 wt_filter_bits=65535);
};

#endif
