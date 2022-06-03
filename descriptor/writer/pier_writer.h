/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DESCRIPTOR_WRITER_PIER_WRITER_H
#define DESCRIPTOR_WRITER_PIER_WRITER_H


#include <string>
#include "obj_writer.h"
#include "../objversion.h"

class pier_writer_t : public obj_writer_t
{
private:
    static pier_writer_t the_instance;

    pier_writer_t() {register_writer(true); }

protected:
    virtual std::string get_node_name(FILE *fp) const {return name_from_next_node(fp); }

public:
    virtual void write_obj(FILE *, obj_node_t &, tabfileobj_t &);

    virtual obj_type get_type() const {return obj_pier; }
    virtual const char * get_type_name() const {return "pier"; }

};

#endif // PIER_WRITER_T_H
