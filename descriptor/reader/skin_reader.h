/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DESCRIPTOR_READER_SKIN_READER_H
#define DESCRIPTOR_READER_SKIN_READER_H


#include "../../simskin.h"
#include "../../tpl/slist_tpl.h"

#include "obj_reader.h"


class skin_reader_t : public obj_reader_t {
public:
	/// @copydoc obj_reader_t::read_node
	obj_desc_t *read_node(FILE *fp, obj_node_info_t &node) OVERRIDE;

protected:
	/// @copydoc obj_reader_t::register_obj
	void register_obj(obj_desc_t *&desc) OVERRIDE;

	/// @copydoc obj_reader_t::successfully_loaded
	bool successfully_loaded() const OVERRIDE;

	/// @returns type of skin this reader is able to read
	virtual skinverwaltung_t::skintyp_t get_skintype() const = 0;
};


class menuskin_reader_t : public skin_reader_t {
	static menuskin_reader_t the_instance;

	menuskin_reader_t() { register_reader(); }
protected:
	/// @copydoc skin_reader_t::get_skintype
	skinverwaltung_t::skintyp_t get_skintype() const OVERRIDE { return skinverwaltung_t::menu; }
public:
	static menuskin_reader_t*instance() { return &the_instance; }

	obj_type get_type() const OVERRIDE { return obj_menu; }
	char const* get_type_name() const OVERRIDE { return "menu"; }
};


class cursorskin_reader_t : public skin_reader_t {
	static cursorskin_reader_t the_instance;

	cursorskin_reader_t() { register_reader(); }
protected:
	/// @copydoc skin_reader_t::get_skintype
	skinverwaltung_t::skintyp_t get_skintype() const OVERRIDE { return skinverwaltung_t::cursor; }
public:
	static cursorskin_reader_t*instance() { return &the_instance; }

	obj_type get_type() const OVERRIDE { return obj_cursor; }
	char const* get_type_name() const OVERRIDE { return "cursor"; }

};


class symbolskin_reader_t : public skin_reader_t {
	static symbolskin_reader_t the_instance;

	symbolskin_reader_t() { register_reader(); }
protected:
	/// @copydoc skin_reader_t::get_skintype
	skinverwaltung_t::skintyp_t get_skintype() const OVERRIDE { return skinverwaltung_t::symbol; }
public:
	static symbolskin_reader_t*instance() { return &the_instance; }

	obj_type get_type() const OVERRIDE { return obj_symbol; }
	char const* get_type_name() const OVERRIDE { return "symbol"; }

};


class fieldskin_reader_t : public skin_reader_t {
	static fieldskin_reader_t the_instance;

	fieldskin_reader_t() { register_reader(); }
protected:
	/// @copydoc skin_reader_t::get_skintype
	skinverwaltung_t::skintyp_t get_skintype() const OVERRIDE { return skinverwaltung_t::nothing; }
public:
	static fieldskin_reader_t *instance() { return &the_instance; }

	obj_type get_type() const OVERRIDE { return obj_field; }
	char const* get_type_name() const OVERRIDE { return "field"; }
};


class smoke_reader_t : public skin_reader_t {
	static smoke_reader_t the_instance;

	smoke_reader_t() { register_reader(); }
protected:
	/// @copydoc skin_reader_t::get_skintype
	skinverwaltung_t::skintyp_t get_skintype() const OVERRIDE { return skinverwaltung_t::nothing; }

public:
	static smoke_reader_t*instance() { return &the_instance; }

	obj_type get_type() const OVERRIDE { return obj_smoke; }
	char const* get_type_name() const OVERRIDE { return "smoke"; }

};


class miscimages_reader_t : public skin_reader_t {
	static miscimages_reader_t the_instance;

	miscimages_reader_t() { register_reader(); }
protected:
	/// @copydoc skin_reader_t::get_skintype
	skinverwaltung_t::skintyp_t get_skintype() const OVERRIDE { return skinverwaltung_t::misc; }
public:
	static miscimages_reader_t*instance() { return &the_instance; }

	obj_type get_type() const OVERRIDE { return obj_miscimages; }
	char const* get_type_name() const OVERRIDE { return "misc"; }
};

#endif
