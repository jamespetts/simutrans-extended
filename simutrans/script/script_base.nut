/**
 * Base file for scripting
 */

// table to hold routines for debug
debug <- {}

function min(a, b) { return a < b ? a : b }
function max(a, b) { return a > b ? a : b }

/**
 * load / save support
 * the persistent table will be written / restored during save / load
 * only plain data is saved: no classes / instances / functions, no cyclic references
 */
persistent <- {}

/**
 * writes the persistent table to a string
 */
function save()
{
	if (typeof(persistent) != "table") {
		throw("error during saving: the variable persistent is not a table")
	}
	local str = "persistent = " + recursive_save(persistent, "\t", [ persistent ] )

	return str
}

function is_identifier(str)
{
	return (str.toalnum() == str)  &&  (str[0] < '0'  ||  str[0] > '9')
}

function recursive_save(table, indent, table_stack)
{
	local isarray = typeof(table) == "array"
	local str = (isarray ? "[" : "{") + "\n"
	foreach(key, val in table) {
		str += indent
		if (!isarray) {
			if (typeof(key)=="string") {
				if (is_identifier(key)) {
					str += key + " = "
				}
				else {
					str += "[\"" + key + "\"] = "
				}
			}
			else {
				str += "[" + key + "] = "
			}
		}
		while( typeof(val) == "weakref" )
			val = val.ref

		switch( typeof(val) ) {
			case "null":
				str += "null"
				break
			case "integer":
			case "float":
			case "bool":
				str += val
				break
			case "string":
				str += "@\"" + val + "\""
				break
			case "array":
			case "table":
				if (!table_stack.find(val)) {
						table_stack.push( table )
						str += recursive_save(val, indent + "\t", table_stack )
						table_stack.pop()
				}
				else {
					// cyclic reference - good luck with resolving
					str += "null"
				}
				break
			case "generator":
				str += "null"
				break
			case "instance":
			default:
				if ("_save" in val) {
					str += val._save()
					break
				}
				str += "\"unknown(" + typeof(val) + ")\""
		}
		if (str.slice(-1) != "\n") {
			str += ",\n"
		}
		else {
			str = str.slice(0,-1) + ",\n"
		}

	}
	str += indent.slice(0,-1) + (isarray ? "]" : "}") + "\n"
	return str
}


/////////////////////////////////////
/**
 * translatable texts
 */
class ttext {
	text_raw = "" // raw text
	text_tra = "" // translated text
	// tables with information to replace variables {key} by their values
	var_strings = null
	var_values  = null
	// these cannot be initialized here to {}, otherwise
	// all instances will refer to the same table!

	constructor(k) {
		if (type(k) != "string") {
			throw("ttext must be initialized with string")
		}
		text_raw = k
		text_tra = k
		var_strings = []
		var_values  = {}
		find_variables(text_raw)
	}

	function get_translated_text()
	{
		return translate(text_raw)
	}

	function find_variables(k) {
		var_strings = []
		// find variable tags
		local reg = regexp(@"\{([a-zA-Z_]+[a-zA-Z_0-9]*)\}");
		local start = 0;
		while (start < k.len()) {
			local res = reg.capture(k, start)
			if (res) {
				local vname = k.slice(res[1].begin, res[1].end)
				var_strings.append({ name = vname, begin = res[0].begin, end = res[0].end })
				start = res[0].end
			}
			else {
				start = k.len();
			}
		}
	}

	function _set(key, val)
	{
		var_values[key] <- val
	}

	function _tostring()
	{
		// get translated text
		local text_tra_ = get_translated_text()
		if (text_tra_ != text_tra) {
			text_tra = text_tra_
			// new text, search for identifiers
			find_variables( text_tra )
		}
		// create array of values, sort it per index in string
		local values = []
		foreach(val in var_strings) {
			local key = val.name
			if ( key in var_values ) {
				local valx = { begin = val.begin, end = val.end, value = var_values[key] }
				values.append(valx)
			}
		}
		values.sort( @(a,b) a.begin <=> b.begin )
		// create the resulting text piece by piece
		local res = ""
		local ind = 0
		foreach( e in values) {
			res += text_tra.slice(ind, e.begin)
			res += e.value
			ind = e.end
		}
		res += text_tra.slice(ind)
		return res
	}
}

/**
 * text from files
 */
class ttextfile extends ttext {

	file = null

	constructor(file_) {
		file = file_
		base.constructor("")
	}

	function get_translated_text()
	{
		return load_language_file(file)
	}


}

/////////////////////////////////////

function _extend_get(index) {
	if (index == "rawin"  ||  index == "rawget") {
		throw null // invoke default delegate
		return
	}
	local fname = "get_" + index
	if (fname in this) {
		local func = this[fname]
		if (typeof(func)=="function") {
			return func.call(this)
		}
	}
	throw null // invoke default delegate
}

/**
 * this class implements an extended get method:
 * everytime an index is not found it tries to call the method 'get_'+index
 */
class extend_get {

	_get = _extend_get

}

/**
 * class that contains data to get access to an in-game factory
 */
class factory_x extends extend_get {
	/// coordinates
	x = -1
	y = -1

	/// input / output slots, will be filled by constructor
	input = {}
	output = {}

	/// constructor will be overwritten by a native method
	constructor(x_, y_) {
		x = x_
		y = y_
	}

	function _tostring() { return "factory_x@" + coord_to_string(this) }
}


/**
 * class that contains data to get access to an production slot of a factory
 */
class factory_production_x extends extend_get {
	/// coordinates of factory
	x = -1
	y = -1
	good = ""  /// name of the good to be consumed / produced
	index = -1 /// index to identify an io slot
	max_storage = 0  /// max storage of this slot
	scaling = 0

	constructor(x_, y_, n_, i_) {
		x = x_
		y = y_
		good  = n_
		index = i_
	}
}


/**
 * class to provide access to the game's list of all factories
 */
class factory_list_x {

	/// meta-method to be called in a foreach loop
	function _nexti(prev_index) {
	}

	/// meta method to retrieve factory by index in the global C++ array
	function _get(index) {
	}
}


/**
 * class that contains data to get access to an in-game player company
 */
class player_x extends extend_get {
	nr = 0 /// player number

	constructor(n_) {
		nr = n_
	}

	function _tostring() { return "player_x@" + nr }
}


/**
 * class that contains data to get access to an in-game halt
 */
class halt_x extends extend_get {
	id = 0 /// halthandle_t

	constructor(i_) {
		id = i_
	}

	function _tostring() { return "halt_x@" + id }
}


/**
 * class that contains data to get access to a line of convoys
 */
class line_x extends extend_get {
	id = 0 /// linehandle_t

	constructor(i_) {
		id = i_
	}

	function _tostring() { return "line_x@" + id }
}

/**
 * class to provide access to line lists
 */
class line_list_x {

	halt_id = 0
	player_id = 0
}

/**
 * class that contains data to get access to a tile (grund_t)
 */
class tile_x extends extend_get {
	/// coordinates
	x = -1
	y = -1
	z = -1

	constructor(x_, y_, z_) {
		x = x_
		y = y_
		z = z_
	}

	function _tostring() { return "tile_x@" + coord3d_to_string(this) }

	function get_objects()
	{
		return tile_object_list_x(x,y,z)
	}
}

class tile_object_list_x {
	/// coordinates
	x = -1
	y = -1
	z = -1

	constructor(x_, y_, z_) {
		x = x_
		y = y_
		z = z_
	}
}

/**
 * class that contains data to get access to a grid square (planquadrat_t)
 */
class square_x extends extend_get {
	/// coordinates
	x = -1
	y = -1

	constructor(x_, y_) {
		x = x_
		y = y_
	}

	function _tostring() { return "square_x@" + coord_to_string(this) }
}


/**
 * class to provide access to convoy lists
 */
class convoy_list_x {

	use_world = 0
	halt_id = 0
	line_id = 0
}

/**
 * class to provide read-only access to a stop's laid-over convoy list
 */
class laid_over_list_x {

	halt_id = 0
}


/**
 * class that contains data to get access to an in-game convoy
 */
class convoy_x extends extend_get {
	id = 0 /// convoihandle_t

	constructor(i_) {
		id = i_
	}

	function _tostring() { return "convoy_x@" + id }
}


/**
 * class to provide access to the game's list of all cities
 */
class city_list_x {

	/// meta-method to be called in a foreach loop
	function _nexti(prev_index) {
	}

	/// meta method to retrieve city by index in the global C++ array
	function _get(index) {
	}
}


/**
 * class that contains data to get access to a city
 */
class city_x extends extend_get {
	/// coordinates
	x = -1
	y = -1

	constructor(x_, y_) {
		x = x_
		y = y_
	}

	function _tostring() { return "city_x@" + coord_to_string(this) }
}

/**
 * class to access in-game settings
 */
class settings {
}

/**
 * base class of map objects (ding_t)
 */
class map_object_x extends extend_get {
	/// coordinates
	x = -1
	y = -1
	z = -1

	function _tostring() { return "map_object_x@" + coord3d_to_string(this) }
}

class schedule_entry_x {
	/*
	 * All values live in a backing table so that the metamethods below can keep
	 * the two naming conventions synchronised. Squirrel calls _get/_set only
	 * for members that are not declared on the class, and "data" is the only
	 * declared member.
	 *
	 * load and minimum_loading are two names for the same uint16 value, and
	 * wait and waiting_time_shift are two names for the same sint8 value.
	 * flags is the raw uint32 bitfield; the named flag values are booleans
	 * derived from it. Assigning any name updates the other representation, so
	 * there is no stored precedence between them: the last assignment wins.
	 */
	data = {}

	static flag_bits = {
		wait_for_time                  = 1 << 0,
		lay_over                       = 1 << 1,
		ignore_choose                  = 1 << 2,
		force_range_stop               = 1 << 3,
		conditional_depart_before_wait = 1 << 4,
		conditional_depart_after_wait  = 1 << 5,
		conditional_skip               = 1 << 6,
		send_trigger                   = 1 << 7,
		cond_trigger_is_line_or_cnv    = 1 << 8,
		clear_stored_triggers_on_dep   = 1 << 9,
		trigger_one_only               = 1 << 10,
		couple                         = 1 << 11,
		uncouple                       = 1 << 12,
		couple_target_is_line_or_cnv   = 1 << 13,
		uncouple_target_is_line_or_cnv = 1 << 14,
		uncouple_target_sch_is_reversed = 1 << 15,
		discharge_payload              = 1 << 16,
		set_down_only                  = 1 << 17,
		pick_up_only                   = 1 << 18,
		couple_target_sch_is_reversed  = 1 << 19
	}

	function _get(key)
	{
		if (typeof(key) == "string" && key in data) {
			return data[key]
		}
		return null
	}

	function _set(key, value)
	{
		if (typeof(key) != "string") {
			return
		}
		if (key == "load" || key == "minimum_loading") {
			data.load <- value
			data.minimum_loading <- value
			return
		}
		if (key == "wait" || key == "waiting_time_shift") {
			data.wait <- value
			data.waiting_time_shift <- value
			return
		}
		if (key == "flags") {
			data.flags <- value
			sync_named_flags(value)
			return
		}
		if (key in flag_bits) {
			data[key] <- value
			if (typeof(value) == "bool") {
				if (value) {
					data.flags = data.flags | flag_bits[key]
				}
				else {
					data.flags = data.flags & ~flag_bits[key]
				}
			}
			return
		}
		data[key] <- value
	}

	function sync_named_flags(value)
	{
		local bits = (typeof(value) == "integer") ? value : 0
		foreach (name, bit in flag_bits) {
			data[name] <- (bits & bit) != 0
		}
	}

	// The _get metamethod resolves every unknown name, so root-table functions
	// must be reached through the explicit root access operator.
	function _tostring() { return "schedule_entry_x@" + ::coord3d_to_string(this) }

	constructor(pos = null, l = 0, w = 0)
	{
		data = {}
		data.x <- -1
		data.y <- -1
		data.z <- -1
		data.load <- 0
		data.minimum_loading <- 0
		data.wait <- 0
		data.waiting_time_shift <- 0
		data.spacing_shift <- 0
		data.unique_entry_id <- 0
		data.flags <- 0
		sync_named_flags(0)
		data.condition_bitfield_broadcaster <- 0
		data.condition_bitfield_receiver <- 0
		data.target_id_condition_trigger <- 0
		data.target_id_couple <- 0
		data.target_id_uncouple <- 0
		data.target_unique_entry_uncouple <- 0
		data.target_unique_entry_couple <- 0
		data.reverse <- -1
		data.max_speed_kmh <- 65535

		if (pos != null) {
			data.x = pos.x
			data.y = pos.y
			data.z = pos.z
		}
		load = l
		wait = w
	}
}

/// One alternative within a consist-order slot.
class vehicle_description_x {
	/// specific vehicle, or null for a rule-based alternative
	specific_vehicle = null
	/// true if the slot may be left empty
	empty = true

	engine_type = 0
	min_catering = 0
	max_catering = 255
	must_carry_class = 0

	min_range = 0
	max_range = 4294967295
	min_brake_force = 0
	max_brake_force = 65535
	min_power = 0
	max_power = 4294967295
	min_tractive_effort = 0
	max_tractive_effort = 4294967295
	min_topspeed = 0
	max_topspeed = 4294967295
	min_weight = 0
	max_weight = 4294967295
	min_axle_load = 0
	max_axle_load = 4294967295
	min_capacity = 0
	max_capacity = 65535
	min_running_cost = 0
	max_running_cost = 4294967295
	min_fixed_cost = 0
	max_fixed_cost = 4294967295
	min_fuel_per_km = 0
	max_fuel_per_km = 4294967295
	min_staff_hundredths = 0
	max_staff_hundredths = 4294967295
	min_drivers = 0
	max_drivers = 4294967295
}


/// One consecutive slot of a consist order.
class consist_order_element_x {
	/// goods category index; -1 means "derive from the alternatives",
	/// consist_order_element_x.any_catg is the wildcard
	catg = -1
	/// prioritised alternatives, in preference order
	alternatives = []

	static any_catg = 255
}


/// The end-state consist order for one schedule entry.
class consist_order_x {
	/// recipient of released vehicles
	available_to_id = 0
	/// 0 = any line or consist; 2 = same line or consist; 1 = convoy
	available_to_flags = 2
	/// consecutive slots, in order
	slots = []
}


/// Read-only snapshot of one vehicle slot in a running convoy.
class vehicle_slot_x {
	/// position in the convoy, 0 = front
	index = -1
	/// vehicle descriptor
	desc = null
	/// owner
	owner = null
	/// travelling backwards in the consist
	reversed = false
	/// cargo units currently carried
	cargo = 0
	/// maximum cargo units
	capacity = 0
}

class dir {
	static none           = 0
	static north          = 1
	static east           = 2
	static northeast      = 3
	static south          = 4
	static northsouth     = 5
	static southeast      = 6
	static northsoutheast = 7
	static west           = 8
	static northwest      = 9
	static eastwest       = 10
	static northeastwest  = 11
	static southwest      = 12
	static northsouthwest = 13
	static southeastwest  = 14
	static all            = 15

	static nsew = [1, 4, 2, 8]
}

class slope {
	static flat=0
	static north = 3+1     ///< North slope
	static west = 9+3      ///< West slope
	static east = 27+1     ///< East slope
	static south = 27+9    ///< South slope
	static northwest = 27  ///< NW corner
	static northeast = 9   ///< NE corner
	static southeast = 3   ///< SE corner
	static southwest = 1   ///< SW corner
	static raised = 80     ///< special meaning: used as slope of bridgeheads
	static all_up_slope   = 82 ///< used for terraforming tools
	static all_down_slope = 83 ///< used for terraforming tools
}

class time_x {
	raw = 1
	year = 0
	month = 1
}

class time_ticks_x extends time_x {
	ticks = 0
	ticks_per_month = 0
	next_month_ticks = 0
}

class coord {
	x = -1
	y = -1

	constructor(_x, _y)  { x = _x; y = _y }
	function _add(other) { return coord(x + other.x, y + other.y) }
	function _sub(other) { return coord(x - other.x, y - other.y) }
	function _mul(fac)   { return coord(x * fac, y * fac) }
	function _div(fac)   { return coord(x / fac, y / fac) }
	function _unm()      { return coord(-x, -y) }
	function _typeof()   { return "coord" }
	function _tostring() { return coord_to_string(this) }
	function _save()     { return "coord(" + x + ", " + y + ")" }
	function href(text)  { return "<a href='(" + x + ", " + y + ")'>" + text + "</a>" }
}

class coord3d extends coord {
	z = -1

	constructor(_x, _y, _z)  { x = _x; y = _y; z = _z }
	function _add(other) { return coord3d(x + other.x, y + other.y, z + getz(other)) }
	function _sub(other) { return coord3d(x - other.x, y - other.y, z - getz(other)) }
	function _mul(fac)   { return coord3d(x * fac, y * fac, z * fac) }
	function _div(fac)   { return coord3d(x / fac, y / fac, z / fac) }
	function _unm()      { return coord3d(-x, -y, -z) }
	function _typeof()   { return "coord3d" }
	function _tostring() { return coord3d_to_string(this) }
	function _save()     { return "coord3d(" + x + ", " + y + ", " + z + ")" }
	function href(text)  { return "<a href='(" + x + ", " + y + ", " + z + ")'>" + text + "</a>" }

	function getz(other) { return ("z" in other) ? other.z : 0 }
}

/**
 * The same metamethod magic as in the class extend_get.
 */
table_with_extend_get <- {

	_get = _extend_get

}

/**
 * table to hold routines to access the world
 */
world <- {}
world.setdelegate(table_with_extend_get)


// table to hold routines for gui access
gui <- {}
