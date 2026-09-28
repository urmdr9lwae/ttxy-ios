#include "KFDB.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "HostFile.h"
#include "HostLog.h"

namespace host {
namespace {

const char kMagic[16] = {'C', 'O', 'P', 'Y', 'R', 'I', 'G', 'H', 'T', '@', 'S', 'Q', 'L', '1', 0, 0};

enum ColumnType : uint8_t { C_U8 = 0x01, C_I32 = 0x04, C_U32 = 0x05, C_DOUBLE = 0x07, C_STRING = 0x0A };

int ColumnWidth(uint8_t t) {
    switch (t) {
        case C_U8: return 1;
        case C_I32: case C_U32: case C_STRING: return 4;
        case C_DOUBLE: return 8;
        default: return -1;
    }
}

uint32_t ReadU32(const std::string& b, size_t off) {
    uint32_t v;
    std::memcpy(&v, b.data() + off, 4);  // 文件是小端，目标平台（x86 / arm）也都是小端
    return v;
}

std::string PoolString(const std::string& b, size_t poolStart, uint32_t off) {
    const size_t p = poolStart + off;
    if (p >= b.size()) return std::string();
    const char* s = b.data() + p;
    const size_t maxLen = b.size() - p;
    return std::string(s, strnlen(s, maxLen));
}

}  // namespace

int StructDef::IndexField() const {
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].isIndex) return static_cast<int>(i);
    return -1;
}

bool Table::Parse(const std::string& b, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (b.size() < 0x20 || std::memcmp(b.data(), kMagic, 16) != 0) return fail("bad magic");
    const uint32_t ncols = ReadU32(b, 0x14), nrows = ReadU32(b, 0x18), poolSize = ReadU32(b, 0x1C);
    const size_t descEnd = 0x20 + static_cast<size_t>(ncols) * 5;
    if (descEnd > b.size() || poolSize > b.size()) return fail("header out of range");
    const size_t poolStart = b.size() - poolSize;

    columns_.clear();
    types_.clear();
    size_t rowWidth = 0;
    for (uint32_t c = 0; c < ncols; ++c) {
        const uint8_t t = static_cast<uint8_t>(b[0x20 + c * 5]);
        const int w = ColumnWidth(t);
        if (w < 0) return fail("unknown column type");
        types_.push_back(t);
        columns_.push_back(PoolString(b, poolStart, ReadU32(b, 0x20 + c * 5 + 1)));
        rowWidth += static_cast<size_t>(w);
    }
    if (descEnd + rowWidth * nrows != poolStart) return fail("row data size mismatch");

    rows_.assign(nrows, std::vector<Cell>(ncols));
    size_t p = descEnd;
    for (uint32_t r = 0; r < nrows; ++r) {
        for (uint32_t c = 0; c < ncols; ++c) {
            Cell& cell = rows_[r][c];
            switch (types_[c]) {
                case C_U8: cell.number = static_cast<unsigned char>(b[p]); p += 1; break;
                case C_I32: cell.number = static_cast<int32_t>(ReadU32(b, p)); p += 4; break;
                case C_U32: cell.number = ReadU32(b, p); p += 4; break;
                case C_DOUBLE: std::memcpy(&cell.number, b.data() + p, 8); p += 8; break;
                case C_STRING:
                    cell.isString = true;
                    cell.text = PoolString(b, poolStart, ReadU32(b, p));
                    p += 4;
                    break;
            }
        }
    }
    return true;
}

int Table::ColumnIndex(const std::string& name) const {
    for (size_t i = 0; i < columns_.size(); ++i)
        if (columns_[i] == name) return static_cast<int>(i);
    return -1;
}

std::string Table::KeyOf(double number) {
    char buf[64];
    if (std::floor(number) == number && std::fabs(number) < 9.2e18)
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number));
    else
        std::snprintf(buf, sizeof(buf), "%.17g", number);
    return buf;
}

std::string Table::KeyOf(const Cell& cell) { return cell.isString ? cell.text : KeyOf(cell.number); }

void Table::BuildIndex(int keyColumn) {
    index_.clear();
    if (columns_.empty()) return;
    if (keyColumn < 0 || keyColumn >= static_cast<int>(columns_.size())) keyColumn = 0;
    for (size_t r = 0; r < rows_.size(); ++r) index_.emplace(KeyOf(rows_[r][keyColumn]), r);  // 重复主键保留第一条
}

long Table::Find(const std::string& key) const {
    auto it = index_.find(key);
    return it == index_.end() ? -1 : static_cast<long>(it->second);
}

Database& Database::Instance() {
    static Database db;
    return db;
}

void Database::AddStruct(const std::string& typeName, const StructDef& def) { structs_[typeName] = def; }

void Database::AddFile(const std::string& fileName, const std::string& desc, const std::string& typeName) {
    Entry& e = files_[typeName];
    e.file = fileName.empty() ? typeName : fileName;
    e.desc = desc;
    e.tried = false;
    e.table.reset();
}

const StructDef* Database::GetStruct(const std::string& typeName) const {
    auto it = structs_.find(typeName);
    return it == structs_.end() ? nullptr : &it->second;
}

const Table* Database::Get(const std::string& typeName) {
    auto it = files_.find(typeName);
    if (it == files_.end()) {
        // 没有 DEF 的表也允许直接按类型名加载（例如运行时新增的表）
        it = files_.emplace(typeName, Entry{typeName, "", false, nullptr}).first;
    }
    Entry& e = it->second;
    if (e.table || e.tried) return e.table.get();
    e.tried = true;
    std::string bytes;
    const std::string path = "db/" + e.file + ".sql";  // 原版命名：db/<文件>.sql（libgame.so 里的 "%s%s.sql"）
    if (!ReadResource(path, bytes)) {
        Log("KFDB: missing table file %s", path.c_str());
        return nullptr;
    }
    auto t = std::make_unique<Table>();
    std::string err;
    if (!t->Parse(bytes, &err)) {
        Log("KFDB: failed to parse %s: %s", path.c_str(), err.c_str());
        return nullptr;
    }
    int keyCol = -1;
    if (const StructDef* s = GetStruct(typeName)) {
        const int idx = s->IndexField();
        if (idx >= 0) keyCol = t->ColumnIndex(s->fields[idx].name);
    }
    t->BuildIndex(keyCol);
    e.table = std::move(t);
    return e.table.get();
}

void Database::Clear() {
    structs_.clear();
    files_.clear();
}

}  // namespace host
