/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DESCRIPTOR_CITYCAR_DESC_H
#define DESCRIPTOR_CITYCAR_DESC_H


#include "obj_base_desc.h"
#include "image_list.h"
#include "../dataobj/ribi.h"
#include "../simtypes.h"
#include "../network/checksum.h"
#include "../tpl/vector_tpl.h"


/**
 * Private city cars, not player owned. They automatically appear in cities.
 *
 * Child nodes:
 *  0   Name
 *  1   Copyright
 *  2   Image-list
 */
class citycar_desc_t : public obj_desc_timelined_t {
	friend class citycar_reader_t;

	uint16 distribution_weight;

	/// topspeed in internal speed units !!! not km/h!!!
	uint16 topspeed;

	// Time-based comfort in tenths of minutes. Zero means "not specified";
	// the getters then use the default of 180 minutes comfortable and three
	// times that tolerable.
	uint16 maximum_comfortable_journey = 0;
	uint16 maximum_tolerable_journey = 0;

	// The passenger wealth classes for which this car is available, as integer
	// class indices (0 is the lowest class). Empty means all classes.
	vector_tpl<uint8> available_classes;

public:
	image_id get_image_id(ribi_t::dir dir) const
	{
		image_t const* const image = get_child<image_list_t>(2)->get_image(dir);
		return image != NULL ? image->get_id() : IMG_EMPTY;
	}

	uint16 get_distribution_weight() const { return distribution_weight; }

	/// topspeed in internal speed units !!! not km/h!!!
	uint16 get_topspeed() const { return topspeed; }

	/// Whether passengers of this wealth class can travel in this car.
	bool is_available_to_class(uint8 g_class) const
	{
		return available_classes.empty() || available_classes.is_contained(g_class);
	}

	/// The passenger wealth classes for which this car is available; empty
	/// means every class.
	const vector_tpl<uint8> &get_available_classes() const { return available_classes; }

	uint16 get_maximum_comfortable_journey() const
	{
		return maximum_comfortable_journey != 0 ? maximum_comfortable_journey : 1800;
	}

	uint16 get_maximum_tolerable_journey() const
	{
		return maximum_tolerable_journey != 0 ? maximum_tolerable_journey : (uint16)min(65535u, (uint32)get_maximum_comfortable_journey() * 3u);
	}

	void calc_checksum(checksum_t *chk) const
	{
		obj_desc_timelined_t::calc_checksum(chk);
		chk->input(distribution_weight);
		chk->input(topspeed);
		chk->input(maximum_comfortable_journey);
		chk->input(maximum_tolerable_journey);
		const uint32 class_count = available_classes.get_count();
		chk->input(class_count);
		for (auto const g_class : available_classes)
		{
			chk->input(g_class);
		}
	}
};

#endif
