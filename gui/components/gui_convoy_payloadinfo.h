/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_CONVOY_PAYLOADINFO_H
#define GUI_COMPONENTS_GUI_CONVOY_PAYLOADINFO_H


#include "gui_container.h"
#include "../../display/scr_coord.h"
#include "../../convoihandle_t.h"

class gui_convoy_payloadinfo_t : public gui_container_t
{
private:
	convoihandle_t cnv;

public:
	gui_convoy_payloadinfo_t(convoihandle_t cnv);

	void set_cnv(convoihandle_t c) { cnv = c; }

	void draw(scr_coord offset);
};


class gui_loadingbar_t : public gui_container_t
{
private:
	convoihandle_t cnv;
	bool size_fixed = false;

public:
	gui_loadingbar_t(convoihandle_t cnv);

	void set_cnv(convoihandle_t c) { cnv = c; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;

	void set_size_fixed(bool yesno) { size_fixed = yesno; };
};

#endif
