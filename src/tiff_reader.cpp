#include "impls/tiff_reader.h"
#include "impls/tiff_pal.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <algorithm>
#include <vector>
#include <type_traits>

namespace tiff {

const std::map<tag_t, std::function<bool(const reader&, const tag_entry&, page&)>> reader::tag_procs = {
    {tag_t::IMAGE_WIDTH, tag_manager::image_width},
    {tag_t::IMAGE_LENGTH, tag_manager::image_length},
    {tag_t::BITS_PER_SAMPLE, tag_manager::bits_per_sample},
    {tag_t::COMPRESSION, tag_manager::compression},
    {tag_t::PHOTOMETRIC_INTERPRETATION, tag_manager::photometric_interpretation},
    {tag_t::STRIP_OFFSETS, tag_manager::strip_offsets},
    {tag_t::ROWS_PER_STRIP, tag_manager::rows_per_strip},
    {tag_t::STRIP_BYTE_COUNTS, tag_manager::strip_byte_counts},
    {tag_t::X_RESOLUTION, tag_manager::x_resolution},
    {tag_t::Y_RESOLUTION, tag_manager::y_resolution},
    {tag_t::PLANAR_CONFIGURATION, tag_manager::planar_configuration},
    {tag_t::RESOLUTION_UNIT, tag_manager::resolution_unit},
    {tag_t::COLOR_MAP, tag_manager::color_map},
    {tag_t::IMAGE_DESCRIPTION, tag_manager::image_description},
    {tag_t::SAMPLES_PER_PIXEL, tag_manager::samples_per_pixel},
    {tag_t::DATE_TIME, tag_manager::date_time},
    {tag_t::EXTRA_SAMPLES, tag_manager::extra_samples},
};

template<typename T>
const std::map<T, const char*> string_map = {};

template<>
const std::map<data_t, const char*> string_map<data_t>;

template<>
const std::map<endian_t, const char*> string_map<endian_t> = {
    {endian_t::INVALID, "INVALID"},
    {endian_t::BIG,     "BIG"},
    {endian_t::LITTLE,  "LITTLE"}
};

template<>
const std::map<tag_t, const char*> string_map<tag_t> = {
    {tag_t::NEW_SUBFILE_TYPE,           "New Subfile Type"},
    {tag_t::IMAGE_WIDTH,                "Image Width"},
    {tag_t::IMAGE_LENGTH,               "Image Length"},
    {tag_t::BITS_PER_SAMPLE,            "Bits/Sample"},
    {tag_t::COMPRESSION,                "Compression"},
    {tag_t::PHOTOMETRIC_INTERPRETATION, "Photometric Interpretation"},
    {tag_t::STRIP_OFFSETS,              "Strip Offsets"},
    {tag_t::ROWS_PER_STRIP,             "Rows/Strip"},
    {tag_t::STRIP_BYTE_COUNTS,          "Strip Byte Counts"},
    {tag_t::X_RESOLUTION,               "X Resolution"},
    {tag_t::Y_RESOLUTION,               "Y Resolution"},
    {tag_t::PLANAR_CONFIGURATION,       "Planar Configuration"},
    {tag_t::RESOLUTION_UNIT,            "Resolution Unit"},
    {tag_t::COLOR_MAP,                  "Color Map"},
    {tag_t::IMAGE_DESCRIPTION,          "Image Description"},
    {tag_t::SAMPLES_PER_PIXEL,          "Samples/Pixel"},
    {tag_t::DATE_TIME,                  "Date Time"},
    {tag_t::EXTRA_SAMPLES,              "Extra Samples"},
};

template<>
const std::map<compression_t, const char*> string_map<compression_t> = {
    {compression_t::NONE,       "None"},
    {compression_t::CCITTRLE,   "CCITT modified Huffman RLE"},
    {compression_t::CCITTFAX3,  "CCITT Group 3 fax encoding"},
    {compression_t::CCITTFAX4,  "CCITT Group 4 fax encoding"},
    {compression_t::LZW,        "LZW"},
    {compression_t::OJPEG,      "JPEG ('old-style' JPEG)"},
    {compression_t::JPEG,       "JPEG ('new-style' JPEG)"},
    {compression_t::DEFLATE,    "Deflate ('Adobe-style', 'zip')"}, // zip
    {compression_t::PACKBITS,   "PackBits"},
};

template<>
const std::map<colorspace_t, const char*> string_map<colorspace_t> = {
    {colorspace_t::MINISWHITE,  "WhiteIsZero"},
    {colorspace_t::MINISBLACK,  "BlackIsZero"},
    {colorspace_t::RGB,         "RGB"},
    {colorspace_t::PALETTE,     "Palette color"},
    {colorspace_t::MASK,        "Transparency Mask"},
    {colorspace_t::SEPARATED,   "CMYK"},
    {colorspace_t::YCBCR,       "YCbCr"},
};

template<>
const std::map<extra_data_t, const char*> string_map<extra_data_t> = {
    {extra_data_t::UNSPECIFIED,     "Unspecified"},
    {extra_data_t::ASSOCALPHA,      "Associated alpha (pre-multiplied aplha)"},
    {extra_data_t::UNASSALPHA,      "Unassociated alpha"},
};

template<>
const std::map<planar_configuration_t, const char*> string_map<planar_configuration_t> = {
    {planar_configuration_t::CONTIG,    "Contig"},
    {planar_configuration_t::SEPARATE,  "Separate"},
};

template<typename T, std::enable_if_t<std::is_enum<T>::value, std::nullptr_t>>
const char* to_string(T e)
{
    if (string_map<T>.count(e)) {
        return string_map<T>.at(e);
    }
    return "UNKNOWN";
}

int32_t page::reserve_page_id()
{
    for (uint32_t i = 0; i < tiff_pal::PIX_BUF_COUNT; i++) {
        if (!tiff_pal::pix_buffer_statics[i].in_use) {
            tiff_pal::pix_buffer_statics[i].in_use = true;
            return i;
        }
    }
    return -1;
}

void page::release_page_id(const int32_t id)
{
    if (id >= static_cast<int32_t>(tiff_pal::PIX_BUF_COUNT)) return;
    tiff_pal::pix_buffer_statics[id].in_use = false;
}

uint8_t page::calc_byte_per_pixel(const uint16_t sample_per_pixel, const std::vector<uint16_t> &bit_per_samples)
{
    printf("spp: %d\n", sample_per_pixel);
    if (bit_per_samples.size() != sample_per_pixel) {
        if (bit_per_samples.size() >= 1) {
            return (sample_per_pixel * bit_per_samples[0]) / 8;
        }
    }

    uint16_t total_bits = 0;
    for (auto& b: bit_per_samples) {
        total_bits += b;
    }
    if (total_bits % 8 != 0) return 0;
    return total_bits / 8;
}

bool page::validate_bit_per_samples(const uint16_t sample_per_pixel, std::vector<uint16_t> &bit_per_samples)
{
    while(bit_per_samples.size() < sample_per_pixel) {
        bit_per_samples.push_back(bit_per_samples[0]);
    }
    return true;
}

void page::print_info() const
{
    printf("Image Width: %d Image Height: %d\n", width, height);
    printf("Bits/Sample: ");
    for (auto& bps: bit_per_samples) {
        printf("%d ", bps);
    }
    printf("\n");
    printf("Compression Scheme: %s\n", to_string(compression));
    printf("Photometric Interpretation: %s\n", to_string(colorspace));
    printf("%ld Strips:\n", strip_offsets.size());
    for (size_t i = 0; i < strip_offsets.size(); i++) {
        printf("\t%ld: [%10d, %10d]\n", i, strip_offsets[i], strip_byte_counts[i]);
    }
    printf("Samples/Pixel: %d\n", sample_per_pixel);
    printf("Rows/Strip: %u\n", rows_per_strip);
    printf("Extra Samples: %u <%s>\n", extra_sample_counts, to_string(extra_sample_type));
    if (description.length() != 0) {
        printf("Description: %s\n", description.c_str());
    }
    if (date_time.length() != 0) {
        printf("Date Time: %s\n", date_time.c_str());
    }
}

int page::get_pixels(const uint16_t x, const uint16_t y, const size_t l, color_t *pixs) const
{
    uint32_t ptr = (y*width + x) * byte_per_pixel;
    uint16_t target_strip = 0;
    uint32_t total_byte = 0;
    for (auto& s: strip_byte_counts) {
        if (total_byte + s > ptr) break;
        total_byte += s;
        target_strip++;
    }

    ptr -= total_byte;

    // Specialized optimization
    if (bit_per_samples == std::vector<uint16_t>{8, 8, 8, 8} && sample_per_pixel == 4 && colorspace == colorspace_t::RGB) {
        r.fread_pos(pixs, strip_offsets[target_strip] + ptr, 4*l);
        return l;
    }

    if (sample_per_pixel == 2 && colorspace == colorspace_t::MINISBLACK) {
        tiff_pal::info_buffer_lock();
        for (size_t i = 0; i < l; i++) {
            r.fread_pos(tiff_pal::info_buffer, strip_offsets[target_strip] + ptr + (i*byte_per_pixel), byte_per_pixel);

            pixs[i].r = extract_memory<uint8_t>(tiff_pal::info_buffer, 0, bit_per_samples[0]);
            pixs[i].g = pixs[i].r;
            pixs[i].b = pixs[i].r;
            pixs[i].a = extract_memory<uint8_t>(tiff_pal::info_buffer, bit_per_samples[0], bit_per_samples[0]);
        }
        tiff_pal::info_buffer_unlock();
        return l;
    }

    // General Processing
    tiff_pal::info_buffer_lock();
    for (size_t i = 0; i < l; i++) {
        r.fread_pos(tiff_pal::info_buffer, strip_offsets[target_strip] + ptr + i*byte_per_pixel, byte_per_pixel);

        uint8_t* c_u8[4] = {&pixs[i].r, &pixs[i].g, &pixs[i].b, &pixs[i].a};
        uint8_t p = 0;
        uint8_t start_pos = 0;
        for (auto& b: bit_per_samples) {
            *c_u8[p++] = extract_memory<uint8_t>(tiff_pal::info_buffer, start_pos, b);
            start_pos += b;
        }
    }
    tiff_pal::info_buffer_unlock();

    return l;
}

color_t page::get_pixel(const uint16_t x, const uint16_t y) const
{
    if (buffer_id == -1) return get_pixel_without_buffering(x, y);
    uint32_t ptr = (y*width + x) * byte_per_pixel;
    uint16_t target_strip = 0;
    uint32_t total_byte = 0;
    for (auto& s: strip_byte_counts) {
        if (total_byte + s > ptr) break;
        total_byte += s;
        target_strip++;
    }

    ptr -= total_byte;

    size_t read_buffer_pos = 0;
    tiff_pal::pix_buffer_lock();
    if (tiff_pal::pix_buffer_statics[buffer_id].strip == target_strip
            && tiff_pal::pix_buffer_statics[buffer_id].start <= ptr
            && tiff_pal::pix_buffer_statics[buffer_id].start + tiff_pal::pix_buffer_statics[buffer_id].len > ptr) {
        read_buffer_pos = ptr - tiff_pal::pix_buffer_statics[buffer_id].start;
    } else {
        const size_t remain = strip_byte_counts[target_strip] - ptr;
        const size_t size = tiff_pal::PIX_BUF_SIZE > remain ? remain : tiff_pal::PIX_BUF_SIZE;
        r.fread_pos(tiff_pal::pix_buffer[buffer_id], strip_offsets[target_strip] + ptr, size);
        tiff_pal::pix_buffer_statics[buffer_id].strip = target_strip;
        tiff_pal::pix_buffer_statics[buffer_id].start = ptr;
        tiff_pal::pix_buffer_statics[buffer_id].len= size;
    }

    color_t c;
    uint8_t* c_u8[4] = {&c.r, &c.g, &c.b, &c.a};
    uint8_t i = 0;
    uint8_t start_pos = 0;
    for (auto& b: bit_per_samples) {
        *c_u8[i++] = extract_memory<uint8_t>(
                tiff_pal::pix_buffer[buffer_id] + read_buffer_pos,
                start_pos,
                b);
        start_pos += b;
    }
    tiff_pal::pix_buffer_unlock();

    return c;
}

color_t page::get_pixel_without_buffering(const uint16_t x, const uint16_t y) const
{
    uint32_t ptr = (y*width + x) * byte_per_pixel;
    uint16_t target_strip = 0;
    uint32_t total_byte = 0;
    for (auto& s: strip_byte_counts) {
        if (total_byte + s > ptr) break;
        total_byte += s;
        target_strip++;
    }

    ptr -= total_byte;

    tiff_pal::info_buffer_lock();
    r.fread_pos(tiff_pal::info_buffer, strip_offsets[target_strip] + ptr, byte_per_pixel);

    color_t c;
    uint8_t* c_u8[4] = {&c.r, &c.g, &c.b, &c.a};
    uint8_t i = 0;
    uint8_t start_pos = 0;
    for (auto& b: bit_per_samples) {
        *c_u8[i++] = extract_memory<uint8_t>(tiff_pal::info_buffer, start_pos, b);
        start_pos += b;
    }
    tiff_pal::info_buffer_unlock();

    return c;
}

reader::reader(const std::string& path) :
    path(path)
{
    source = tiff_pal::fopen(path.c_str(), "rb");
    if (source <= 0) {
        return;
    }
    if (!read_header()) {
        tiff_pal::fclose(source);
        source = 0;
        return;
    }
    if (!decode()) {
        return;
    }
}

reader::~reader()
{
    if (is_valid()) {
        tiff_pal::fclose(source);
    }
}

reader reader::open(const std::string& path)
{
    return reader(path);
}

reader *reader::open_ptr(const std::string& path)
{
    return new reader(path);
}

bool reader::is_valid() const
{
    return source && decoded;
}

bool reader::read_header()
{
    tiff_pal::info_buffer_lock();
    fread_pos(tiff_pal::info_buffer, 0, 8);
    {
        buffer_reader r(tiff_pal::info_buffer);
        r.read_array(h.order);
        endian_t t = check_endian_type(h.order);
        if (t == endian_t::INVALID) return false;
        endi = t;
        need_swap = platform_is_big_endian() != is_big_endian();
        r.set_swap_mode(need_swap);
        r.read(h.version);
        r.read(h.offset);
    }
    tiff_pal::info_buffer_unlock();

    if (h.version != 42) return false;
    return true;
}

endian_t reader::check_endian_type(const char* const s)
{
    if (std::memcmp(s, "MM", 2) == 0) {
        return endian_t::BIG;
    } else if (std::memcmp(s, "II", 2) == 0) {
        return endian_t::LITTLE;
    } else {
        return endian_t::INVALID;
    }
}

size_t reader::fread_pos(void* dest, const size_t pos, const size_t size) const
{
    tiff_pal::fseek(source, pos, SEEK_SET);
    tiff_pal::fread(reinterpret_cast<uint8_t*>(dest), size, 1, source);
    return size;
}

bool reader::is_big_endian() const
{
    return endi == endian_t::BIG;
}

bool reader::is_little_endian() const
{
    return endi == endian_t::LITTLE;
}

void reader::fetch_ifds(std::vector<ifd> &ifds) const
{

    tiff_pal::info_buffer_lock();

    // TODO: In rare cases, there may be more multiple IFD.
    ifds.resize(1);
    buffer_reader r(tiff_pal::info_buffer, need_swap);
    fread_pos(tiff_pal::info_buffer, h.offset, sizeof(ifd::entry_count));
    r.read(ifds[0].entry_count);
    ifds[0].entries.resize(ifds[0].entry_count);

    size_t e_size = 12;
    for (int i = 0; i < ifds[0].entry_count; i++) {
        fread_pos(tiff_pal::info_buffer, h.offset + 2 + i * e_size, e_size);
        r.seek_top();
        r.read(ifds[0].entries[i].tag);
        r.read(ifds[0].entries[i].field_type);
        r.read(ifds[0].entries[i].field_count);
        r.read(ifds[0].entries[i].data_field);
    }
    fread_pos(tiff_pal::info_buffer, h.offset, sizeof(ifd::next_ifd));
    r.read(ifds[0].next_ifd);

    tiff_pal::info_buffer_unlock();
}

bool reader::read_entry_tags(const std::vector<ifd> &ifds, std::vector<page> &pages)
{
    uint32_t page_index = 0;
    for (auto& ifd: ifds) {
        for(auto& e: ifd.entries) {
            if (tag_procs.count(e.tag)) {
                if(!tag_procs.at(e.tag)(*this, e, pages[page_index])) {
                    printf("Tag %s(", to_string(e.tag));
                    printf("0x%04X) process failed.\n", enum_base_cast(e.tag));
                    return false;
                }
            } else {
                // printf("Tags id: 0x%04X is not implemented.\n", enum_base_cast(e.tag));
            }
        }
        page_index++;
    }

    for (auto& page: pages) {
        page.validate();
    }
    return true;
}

bool reader::decode()
{
    fetch_ifds(ifds);
    for (size_t i = 0; i < ifds.size(); i++) {
        pages.push_back(page(*this));
    }

    if(!read_entry_tags(ifds, pages)) {
        return false;
    }

    decoded = true;
    return true;
}

const page& reader::get_page(uint32_t index) &
{
    return pages[index];
}

uint32_t reader::get_page_count() const
{
    return pages.size();
}

void reader::print_header() const
{
    printf("order: %.2s\n", h.order);
    printf("version: %d\n", h.version);
    printf("offset: %u\n", h.offset);
}

bool reader::tag_manager::image_width(const reader &r, const tag_entry &e, page& p)
{
    p.width = read_scalar_generic(r, e);
    return true;
}
bool reader::tag_manager::image_length(const reader &r, const tag_entry &e, page& p)
{
    p.height = read_scalar_generic(r, e);
    return true;
}
bool reader::tag_manager::bits_per_sample(const reader &r, const tag_entry &e, page& p)
{
    if (e.data_field <= 0) return false;
    if (e.field_count * sizeof(uint16_t) > sizeof(uint32_t)) {
        p.bit_per_samples.resize(e.field_count);
        uint32_t ptr = read_scalar<uint32_t>(r, e);
        tiff_pal::info_buffer_lock();
        r.fread_array_buffering(p.bit_per_samples, tiff_pal::info_buffer, tiff_pal::INFO_BUF_SIZE, ptr);
        tiff_pal::info_buffer_unlock();
    } else {
        p.bit_per_samples.resize(1);
        p.bit_per_samples[0] = read_scalar<uint16_t>(r, e);
    }
    return true;
}
bool reader::tag_manager::compression(const reader &r, const tag_entry &e, page& p)
{
    auto c = static_cast<compression_t>(read_scalar<uint16_t>(r, e));
    if (c != compression_t::NONE) {
        printf("Compressed tiff is not supported.\n");
        return false;
    }
    p.compression = c;
    return true;
}
bool reader::tag_manager::photometric_interpretation(const reader &r, const tag_entry &e, page& p)
{
    p.colorspace = static_cast<colorspace_t>(read_scalar<uint16_t>(r, e));
    switch (p.colorspace) {
    case colorspace_t::MINISBLACK:
    case colorspace_t::MINISWHITE:
    case colorspace_t::RGB:
    case colorspace_t::PALETTE:
    case colorspace_t::MASK:
        return true;
    default:
        printf("Specified color space is not available.\n");
        return false;
    }
}
bool reader::tag_manager::strip_offsets(const reader &r, const tag_entry &e, page& p)
{
    if (e.data_field <= 0) return false;
    p.strip_offsets.resize(e.field_count);
    if (e.field_count >= 2) {
        uint32_t ptr = read_scalar<uint32_t>(r, e);
        tiff_pal::info_buffer_lock();
        r.fread_array_buffering(p.strip_offsets, tiff_pal::info_buffer, tiff_pal::INFO_BUF_SIZE, ptr);
        tiff_pal::info_buffer_unlock();
    } else {
        p.strip_offsets[0] = read_scalar<uint32_t>(r, e);
    }
    return true;
}
bool reader::tag_manager::rows_per_strip(const reader &r, const tag_entry &e, page& p)
{
    p.rows_per_strip = read_scalar_generic(r, e);
    return true;
}
bool reader::tag_manager::strip_byte_counts(const reader &r, const tag_entry &e, page& p)
{
    if (e.data_field <= 0) return false;
    p.strip_byte_counts.resize(e.field_count);
    if (e.field_count >= 2) {
        uint32_t ptr = read_scalar<uint32_t>(r, e);
        tiff_pal::info_buffer_lock();
        r.fread_array_buffering(p.strip_byte_counts, tiff_pal::info_buffer, tiff_pal::INFO_BUF_SIZE, ptr);
        tiff_pal::info_buffer_unlock();
    } else {
        p.strip_byte_counts[0] = read_scalar<uint32_t>(r, e);
    }
    return true;
}
bool reader::tag_manager::x_resolution(const reader&, const tag_entry&, page&)
{
    // Not yet implemented
    return true;
}
bool reader::tag_manager::y_resolution(const reader&, const tag_entry&, page&)
{
    // Not yet implemented
    return true;
}
bool reader::tag_manager::planar_configuration(const reader &r, const tag_entry &e, page &p)
{
    p.planar_configuration = static_cast<planar_configuration_t>(read_scalar<uint16_t>(r, e));
    if (p.planar_configuration != planar_configuration_t::CONTIG) return false;
    return true;
}
bool reader::tag_manager::resolution_unit(const reader&, const tag_entry&, page&)
{
    // Not yet implemented
    return true;
}
bool reader::tag_manager::color_map(const reader &r, const tag_entry &e, page& p)
{
    if (e.field_count <= 0) return true;

    uint32_t ptr = read_scalar<uint32_t>(r, e);
    if (ptr == 0) return false;

    p.color_palette.resize(e.field_count);
    tiff_pal::info_buffer_lock();
    r.fread_array_buffering(p.color_palette, tiff_pal::info_buffer, tiff_pal::INFO_BUF_SIZE, ptr);
    tiff_pal::info_buffer_unlock();
    return true;
}
bool reader::tag_manager::image_description(const reader &r, const tag_entry &e, page& p)
{
    if (e.field_count <= 0) return true;

    if (e.field_count <= 4) {
        uint32_t u32_str = read_scalar<uint32_t>(r, e);
        std::string temp_str(reinterpret_cast<char*>(&u32_str), 0, e.field_count-1);
        p.description.swap(temp_str);
    } else {
        std::vector<uint8_t> temp_vec(e.field_count, 0);
        uint32_t ptr = read_scalar<uint32_t>(r, e);
        tiff_pal::info_buffer_lock();
        r.fread_array_buffering(temp_vec, tiff_pal::info_buffer, tiff_pal::INFO_BUF_SIZE, ptr);
        tiff_pal::info_buffer_unlock();
        std::string temp_str(temp_vec.begin(), temp_vec.end()-1);
        p.description.swap(temp_str);
    }
    return true;
}
bool reader::tag_manager::samples_per_pixel(const reader &r, const tag_entry &e, page& p)
{
    p.sample_per_pixel = read_scalar_generic(r, e);
    return true;
}
bool reader::tag_manager::date_time(const reader &r, const tag_entry &e, page& p)
{
    if (e.field_count != 20) return false;

    std::vector<uint8_t> temp_vec(20, 0);
    uint32_t ptr = read_scalar<uint32_t>(r, e);
    tiff_pal::info_buffer_lock();
    r.fread_array_buffering(temp_vec, tiff_pal::info_buffer, tiff_pal::INFO_BUF_SIZE, ptr);
    tiff_pal::info_buffer_unlock();
    std::string temp_str(temp_vec.begin(), temp_vec.end()-1);
    p.date_time.swap(temp_str);
    return true;
}
bool reader::tag_manager::extra_samples(const reader &r, const tag_entry &e, page& p)
{
    p.extra_sample_counts = e.field_count;
    p.extra_sample_type = static_cast<extra_data_t>(read_scalar<uint16_t>(r, e));
    return true;
}

}
