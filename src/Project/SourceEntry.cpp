#include "SourceEntry.hpp"

SourceEntry::SourceEntry(const std::filesystem::path& path) : m_path(path) {
    Log.trace("Create SourceEntry {}", path.string()); //
}