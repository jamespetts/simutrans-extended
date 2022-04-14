/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef OBJ_PIER_H
#define OBJ_PIER_H


class karte_t;
class building_desc_t;

#include "../descriptor/pier_desc.h"
#include "simobj.h"

class pier_t : public obj_no_info_t
{
private:
    const pier_desc_t *desc;

    image_id back_image;
    image_id front_image;

    uint8_t rotation;
    bool bad_load;

protected:
    void rdwr(loadsave_t *file) override;

public:
    pier_t(loadsave_t *file);
    pier_t(koord3d pos, player_t *player, const pier_desc_t *desc, uint8_t rot);

    ~pier_t(){
        dbg->message("pier_t","pier destroyed");
        return;
    }

    const pier_desc_t *get_desc() const {return desc; }
    uint8_t get_rotation() const {return rotation; }

    ribi_t::ribi get_above_ribi() const {return desc->get_above_way_ribi(rotation);}
    ribi_t::ribi get_below_ribi() const {return desc->get_below_way_ribi(rotation);}

    uint64 get_base_mask() const {return desc->get_base_mask(rotation);}
    uint64 get_middle_mask() const {return desc->get_middle_mask(rotation);}
    uint64 get_support_mask() const {return desc->get_support_mask(rotation);}
    uint32 get_sub_mask() const {return desc->get_sub_obj_mask();}
    uint32 get_maxspeed() const {return desc->get_topspeed();}
    uint32 get_axle_load() const {return desc->get_max_axle_load();}
    uint32 get_deck_obj_mask() const {return desc->get_deck_obj_mask();}
    bool get_low_waydeck() const {return  desc->get_low_waydeck();}
    bool get_above_way_supplement() const {return  desc->get_above_way_supplement();}

    bool is_bad_load() const {return bad_load;}

    //get the total for entire tile
    static ribi_t::ribi get_above_ribi_total(const grund_t* gr, bool gr_is_base=false);
    static ribi_t::ribi get_below_ribi_total(const grund_t* gr);

    static uint64 get_base_mask_total(const grund_t* gr);
    static uint64 get_middle_mask_total(const grund_t* gr);
    static uint64 get_support_mask_total(const grund_t* gr);
    static uint16 get_speed_limit_deck_total(const grund_t* gr, uint16 maxspeed=-1);
    static uint16 get_max_axle_load_deck_total(const grund_t* gr, uint16 maxload=-1);
    static uint32 get_deck_obj_mask_total(const grund_t* gr);
    static uint32 get_sub_mask_total(const grund_t* gr);

    static bool check_sub_masks(uint32 pier1, uint32 building1, uint32 pier2=0, uint32 building2=0);
    static const char* check_building(const building_desc_t* desc, koord3d pos);

    static const grund_t* ground_below(const grund_t *gr);
    static const grund_t* ground_below(koord3d pos);

    image_id get_image() const override;
    image_id get_front_image() const override;

    const char *get_name() const OVERRIDE {return "Pier";}
#ifdef INLINE_OBJ_TYPE
#else
    typ get_typ() const OVERRIDE { return pier; }
#endif

	void calc_image() override;

	bool check_season(const bool calc_only_season_change) OVERRIDE
	{
		if(  !calc_only_season_change  ) {
			calc_image();
		}
		return true;
	}

	void finish_rd() override;

	void cleanup(player_t *player) override;

	void rotate90() override;

	const char *is_deletable(const player_t *player) override;
};

class parapet_t : public obj_no_info_t {
private:
    const pier_desc_t *desc;

    image_id back_image;
    image_id front_image;

    uint8 rotation : 3;
    uint8 extra_ways : 2;
    uint8 hidden : 1;

    static void set_hidden_all(uint8 tohide, koord3d pos);

protected:
    void rdwr(loadsave_t *file) override;

public:
    parapet_t(loadsave_t *file);
    parapet_t(koord3d pos, player_t *player, const pier_desc_t *desc, uint8_t rot);

    void update_extra_ways(ribi_t::ribi full_ribi);
    void hide(){
        hidden=1;
        calc_image();
    }
    void unhide(){
        hidden=0;
        calc_image();
    }

    static void hide_all(koord3d pos){
        set_hidden_all(1,pos);
    }

    static void unhide_all(koord3d pos){
        set_hidden_all(0,pos);
    }

    const pier_desc_t *get_desc() const {return desc;}
    uint8_t get_rotation() const {return rotation;}

    image_id get_image() const override;
    image_id get_front_image() const override;

    const char *get_name() const OVERRIDE {return "Parapet";}
#ifdef INLINE_OBJ_TYPE
#else
    typ get_typ() const OVERRIDE { return pier; }
#endif

	void calc_image() override;

	bool check_season(const bool calc_only_season_change) OVERRIDE
	{
		if(  !calc_only_season_change  ) {
			calc_image();
		}
		return true;
	}

	void finish_rd() override;

	void cleanup(player_t *player) override;

	void rotate90() override;

	const char *is_deletable(const player_t *player) override;
};

#endif // PIER_H
