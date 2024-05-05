/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef IO_CLASSIFY_FILE_H
#define IO_CLASSIFY_FILE_H


#include "../simtypes.h"


enum file_classify_status_t {
	FILE_CLASSIFY_OK = 0,
	FILE_CLASSIFY_INVALID_ARGS,
	FILE_CLASSIFY_NOT_EXISTING
};


#define INVALID_FILE_VERSION 0xFFFFFFFFu


struct extended_version_t
{
public:
	extended_version_t() : version(INVALID_FILE_VERSION), extended_version(0), extended_revision(0) {}
	extended_version_t(uint32 version, uint32 extended_version, uint32 extended_revision) : version(version), extended_version(extended_version), extended_revision(extended_revision) {}

	static const extended_version_t INVALID;

public:
	uint32 version;
	uint32 extended_version;
	uint32 extended_revision;
};

struct file_info_t
{
	enum file_type_t {
		TYPE_RAW = 0, // either raw binary or text (dat etc)

		TYPE_ZIPPED,  // zipped save
		TYPE_BZIP2,   // bzip2 compressed save
		TYPE_ZSTD,    // zstd compressed save

		TYPE_PNG,     // PNG image
		TYPE_BMP,
		TYPE_PPM,

		TYPE_XML        = 1u << 31,

		// Combined file formats
		TYPE_XML_ZIPPED = TYPE_XML | TYPE_ZIPPED,
		TYPE_XML_BZIP2  = TYPE_XML | TYPE_BZIP2,
		TYPE_XML_ZSTD   = TYPE_XML | TYPE_ZSTD
	};

public:
	file_info_t();
	explicit file_info_t(file_type_t file_type, extended_version_t version = extended_version_t());

public:
	file_type_t file_type;
	extended_version_t ext_version;
	char pak_extension[256]; ///< Pak extension of saved game.
	size_t header_size;      ///< Header size in bytes
};
ENUM_BITSET(file_info_t::file_type_t);


/**
 * Classify a file.
 * @param path must a valid system name, either a short name for windows or UTF8 for other plattforms
 * @param file_info_t If successfully classified, holds information about file format and version.
 *                    Must not be NULL.
 * @returns FILE_ERROR_OK iff successfully classified.
 */
file_classify_status_t classify_file(const char *path, file_info_t *info);

/**
 * Classify an image file.
 * @param path must a valid system name, either a short name for windows or UTF8 for other plattforms
 * @param info If successfully classified, holds information about file format and version.
 *             Must not be NULL.
 * @returns FILE_CLASSIFY_OK iff successfully classified.
 */
file_classify_status_t classify_image_file(const char *path, file_info_t *info);


#endif
