#pragma once

namespace mousedb {
namespace database {
namespace sstable {
class SSTable {
   public:
    SSTable(const std::string &filename);
    void add(const std::string &key, const std::string &value);
    bool get(const std::string &key, std::string &value) const;
    void remove(const std::string &key);
    void flush();

   private:
    std::string filename;
    // Other private members for managing the SSTable
};

}  // namespace sstable
}  // namespace database
}  // namespace mousedb
