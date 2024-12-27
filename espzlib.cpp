#include "espmanger.h"

#include "zlib\zlib.h"

// Function to compress the data and store it in the output parameter
bool CEsp::compress_data(const char* input_data, size_t input_size, std::vector<char>& compressed_data) 
{
    // Initialize the compressor object
    z_stream deflateStream;
    deflateStream.zalloc = Z_NULL;
    deflateStream.zfree = Z_NULL;
    deflateStream.opaque = Z_NULL;
    deflateStream.avail_in = static_cast<uInt>(input_size); // Cast input_size to uInt
    deflateStream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input_data));

    int ret = deflateInit(&deflateStream, Z_BEST_COMPRESSION);
    if (ret != Z_OK) {
        std::cerr << "Failed to initialize compression stream." << std::endl;
        return false;
    }

    // Reserve initial space for compressed data
    uLong combound = static_cast<uLong>(input_size);
    compressed_data.resize(compressBound(combound)); // Estimate the maximum possible compressed size

    deflateStream.next_out = reinterpret_cast<Bytef*>(&compressed_data[0]);
    deflateStream.avail_out = static_cast<uInt>(compressed_data.size());

    // Compress the data
    ret = deflate(&deflateStream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "Failed to compress data." << std::endl;
        deflateEnd(&deflateStream);
        return false;
    }

    // Finalize compression
    deflateEnd(&deflateStream);

    // Resize the buffer to the actual compressed size
    compressed_data.resize(deflateStream.total_out);

    return true;
}


// Function to decompress the data and store it in the output parameter
bool CEsp::decompress_data(const char* compressed_data, size_t compressed_size, std::vector<char>& decompressed_data, size_t decompressed_size) 
{
    // Initialize the decompressor object
    z_stream inflateStream;
    inflateStream.zalloc = Z_NULL;
    inflateStream.zfree = Z_NULL;
    inflateStream.opaque = Z_NULL;
    inflateStream.avail_in = static_cast<uInt>(compressed_size);
    inflateStream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data));

    int ret = inflateInit(&inflateStream);
    if (ret != Z_OK) {
        std::cerr << "Failed to initialize decompression stream." << std::endl;
        return false;
    }

    // Reserve space for the decompressed data based on the provided decompressed_size
    size_t compressed_size__justincase_overflow_buffer = decompressed_size * 3;
    decompressed_data.resize(decompressed_size + compressed_size__justincase_overflow_buffer);

    inflateStream.next_out = reinterpret_cast<Bytef*>(&decompressed_data[0]);
    inflateStream.avail_out = static_cast<uInt>(decompressed_size);

    // Decompress the data
    ret = inflate(&inflateStream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "Failed to decompress data." << std::endl;
        inflateEnd(&inflateStream);
        return false;
    }

    // Finalize decompression
    inflateEnd(&inflateStream);

    // Resize the vector to the actual decompressed size (in case it's different)
    decompressed_data.resize(decompressed_size - inflateStream.avail_out);

    return true;
}