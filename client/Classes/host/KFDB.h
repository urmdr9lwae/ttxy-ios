// KFDB：游戏数据表（.sqldat，文件头 "COPYRIGHT@SQL1"）读取。
//
// 文件格式（小端）：
//   0x00 char[16] "COPYRIGHT@SQL1\0\0"
//   0x10 u32 版本标记 0x01332C02
//   0x14 u32 列数   0x18 u32 行数   0x1C u32 字符串池大小
//   0x20 列描述 * 列数：u8 类型 + u32 列名在字符串池中的偏移
//   之后是定长行数据，最后是字符串池
// 列类型：0x01 u8、0x04 i32、0x05 u32、0x07 double、0x0A 字符串（u32 池偏移）
//
// 表结构由 Lua（EnvConfig.lua 的 DEF）注册：类型名、字段、主键字段、对应文件名。
// 数据文件在导出目录里的路径：db/<文件名>.sql
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace host {

// DEF 里声明的字段类型（数值与 Lua 全局常量 T_* 一致）
enum FieldType : int { T_UINT = 1, T_INT = 2, T_DOUBLE = 3, T_INT64 = 4, T_LPCSTR = 5 };

struct FieldDef {
    int type = T_LPCSTR;
    std::string name;
    bool isIndex = false;
    std::string desc;
};

struct StructDef {
    std::vector<FieldDef> fields;
    int IndexField() const;  // 主键字段下标，没有则 -1
};

// 一个单元格的值：数字或字符串
struct Cell {
    bool isString = false;
    double number = 0;
    std::string text;
};

class Table {
public:
    // 解析 .sqldat 内容；失败时返回 false 并写 error
    bool Parse(const std::string& bytes, std::string* error);

    size_t RowCount() const { return rows_.size(); }
    const std::vector<std::string>& Columns() const { return columns_; }
    const std::vector<Cell>& Row(size_t i) const { return rows_[i]; }
    int ColumnIndex(const std::string& name) const;

    // 按主键列建索引；keyColumn 为 -1 时用第 0 列
    void BuildIndex(int keyColumn);
    // 按主键查行号，找不到返回 -1
    long Find(const std::string& key) const;

    // 数字主键统一转成字符串，保证 11 和 11.0 能命中同一条
    static std::string KeyOf(double number);
    static std::string KeyOf(const Cell& cell);

private:
    std::vector<std::string> columns_;
    std::vector<uint8_t> types_;
    std::vector<std::vector<Cell>> rows_;
    std::unordered_map<std::string, size_t> index_;
};

class Database {
public:
    static Database& Instance();

    void AddStruct(const std::string& typeName, const StructDef& def);
    void AddFile(const std::string& fileName, const std::string& desc, const std::string& typeName);

    // 取表（首次访问时从 db/<文件名>.sqldat 加载），失败返回 nullptr
    const Table* Get(const std::string& typeName);
    const StructDef* GetStruct(const std::string& typeName) const;

    void Clear();

private:
    struct Entry {
        std::string file;
        std::string desc;
        bool tried = false;
        std::unique_ptr<Table> table;
    };
    std::unordered_map<std::string, StructDef> structs_;
    std::unordered_map<std::string, Entry> files_;  // 类型名 -> 文件
};

}  // namespace host
