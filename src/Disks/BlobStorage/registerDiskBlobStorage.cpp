#if !defined(ARCADIA_BUILD)
#include <Common/config.h>
#endif

#include <Disks/DiskFactory.h>

#if USE_AZURE_BLOB_STORAGE

#include <Disks/BlobStorage/DiskBlobStorage.h>
#include <Disks/DiskRestartProxy.h>
#include <Disks/DiskCacheWrapper.h>
#include <azure/identity/managed_identity_credential.hpp>
#include <re2/re2.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int PATH_ACCESS_DENIED;
    extern const int FILE_DOESNT_EXIST;
    extern const int FILE_ALREADY_EXISTS;
}

constexpr char test_file[] = "test.txt";
constexpr char test_str[] = "test";
constexpr size_t test_str_size = 4;


void checkWriteAccess(IDisk & disk)
{
    auto file = disk.writeFile(test_file, DBMS_DEFAULT_BUFFER_SIZE, WriteMode::Rewrite);
    file->write(test_str, test_str_size);
}


void checkReadAccess(IDisk & disk)
{
    auto file = disk.readFile(test_file, DBMS_DEFAULT_BUFFER_SIZE);
    String buf(test_str_size, '0');
    file->readStrict(buf.data(), test_str_size);
    if (buf != test_str)
        throw Exception("No read access to disk", ErrorCodes::PATH_ACCESS_DENIED);
}


// TODO: make it a unit test ?
void checkReadWithOffset(IDisk & disk)
{
    auto file = disk.readFile(test_file, DBMS_DEFAULT_BUFFER_SIZE);
    auto offset = 2;
    auto test_size = test_str_size - offset;
    String buf(test_size, '0');
    file->seek(offset, 0);
    file->readStrict(buf.data(), test_size);
    if (buf != test_str + offset)
        throw Exception("Failed to read file with offset", ErrorCodes::PATH_ACCESS_DENIED);
}


void checkRemoveAccess(IDisk & disk)
{
    // TODO: remove these checks if the names of blobs will be changed
    if (!disk.checkUniqueId(test_file))
        throw Exception(ErrorCodes::FILE_DOESNT_EXIST, "Expected the file to exist, but did not find it: {}", test_file);

    disk.removeFile(test_file);

    if (disk.checkUniqueId(test_file))
        throw Exception(ErrorCodes::FILE_ALREADY_EXISTS, "Expected the file not to exist, but found it: {}", test_file);
}


void validate_endpoint_url(const String & endpoint_url)
{
    const auto * endpoint_url_pattern_str = R"(http(()|s)://[a-z0-9-]+\.blob\.core\.windows\.net/[a-z0-9-]+)";
    static const RE2 endpoint_url_pattern(endpoint_url_pattern_str);

    if (!re2::RE2::FullMatch(endpoint_url, endpoint_url_pattern))
    {
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Blob Storage URL is not valid, should follow the format: {}, got: {}", endpoint_url_pattern_str, endpoint_url);
    }
}


std::unique_ptr<DiskBlobStorageSettings> getSettings(const Poco::Util::AbstractConfiguration & config, const String & config_prefix, ContextPtr /* context */)
{
    return std::make_unique<DiskBlobStorageSettings>(
        config.getUInt64(config_prefix + ".max_single_part_upload_size", 100 * 1024 * 1024),
        config.getUInt64(config_prefix + ".min_bytes_for_seek", 1024 * 1024),
        config.getInt(config_prefix + ".thread_pool_size", 16),
        config.getInt(config_prefix + ".objects_chunk_size_to_delete", 1000)
        // TODO: use context for global settings from Settings.h
        );
}


void registerDiskBlobStorage(DiskFactory & factory)
{
    auto creator = [](
        const String & name,
        const Poco::Util::AbstractConfiguration & config,
        const String & config_prefix,
        ContextPtr context,
        const DisksMap &)
    {
        auto endpoint_url = config.getString(config_prefix + ".endpoint");
        validate_endpoint_url(endpoint_url);

        auto managed_identity_credential = std::make_shared<Azure::Identity::ManagedIdentityCredential>();
        auto blob_container_client = std::make_shared<Azure::Storage::Blobs::BlobContainerClient>(endpoint_url, managed_identity_credential);

        /// where the metadata files are stored locally
        auto metadata_path = config.getString(config_prefix + ".metadata_path", context->getPath() + "disks/" + name + "/");
        fs::create_directories(metadata_path);

        std::shared_ptr<IDisk> blob_storage_disk = std::make_shared<DiskBlobStorage>(
            name,
            metadata_path,
            blob_container_client,
            getSettings(config, config_prefix, context),
            getSettings
        );

        // NOTE: test - almost direct copy-paste from registerDiskS3
        if (!config.getBool(config_prefix + ".skip_access_check", false))
        {
            checkWriteAccess(*blob_storage_disk);
            checkReadAccess(*blob_storage_disk);
            checkReadWithOffset(*blob_storage_disk);
            checkRemoveAccess(*blob_storage_disk);
        }

        // NOTE: cache - direct copy-paste from registerDiskS3
        if (config.getBool(config_prefix + ".cache_enabled", true))
        {
            String cache_path = config.getString(config_prefix + ".cache_path", context->getPath() + "disks/" + name + "/cache/");

            if (metadata_path == cache_path)
                throw Exception("Metadata and cache path should be different: " + metadata_path, ErrorCodes::BAD_ARGUMENTS);

            auto cache_disk = std::make_shared<DiskLocal>("blob-storage-cache", cache_path, 0);
            auto cache_file_predicate = [] (const String & path)
            {
                return path.ends_with("idx") // index files.
                       || path.ends_with("mrk") || path.ends_with("mrk2") || path.ends_with("mrk3") /// mark files.
                       || path.ends_with("txt") || path.ends_with("dat");
            };

            blob_storage_disk = std::make_shared<DiskCacheWrapper>(blob_storage_disk, cache_disk, cache_file_predicate);
        }

        return std::make_shared<DiskRestartProxy>(blob_storage_disk);
    };
    factory.registerDiskType("blob_storage", creator);
}

}

#else

namespace DB
{

void registerDiskBlobStorage(DiskFactory &) {}

}

#endif
