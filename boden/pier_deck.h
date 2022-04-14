/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef BODEN_PIER_DECK_H
#define BODEN_PIER_DECK_H


#include "grund.h"
#include "../dataobj/ribi.h"

class pier_deck_t : public grund_t
{
private:
    slope_t::type way_slope;
    bool is_dummy;
protected:
    void calc_image_internal(const bool calc_only_snowline_change) override;

public:
    pier_deck_t(loadsave_t *file, koord pos) : grund_t(koord3d(pos,0) ) {rdwr(file); is_dummy=false;}
    pier_deck_t(koord3d pos, slope_t::type grund_slope, slope_t::type way_slope);

    ~pier_deck_t() {
        dbg->message("pier_deck_t","deck destroyed");
        return;
    }

    virtual void rdwr(loadsave_t *file) override;

    virtual void rotate90() override;

    virtual sint8 get_weg_yoff() const override;

    slope_t::type get_weg_hang() const override {return way_slope; }

    const char * get_name() const override {return "Pierdeck"; }
    typ get_typ() const override {return pierdeck; }

    void info(cbuffer_t &buf) const override;

    void set_dummy(bool dummy=true) {is_dummy=dummy;}
    bool get_is_dummy(){return is_dummy;}
};

#endif // PIER_DECK_H
