#pragma once
// Public, UI-independent formal protocol schema. All manual quantities use the
// units in labels.
#include "Json.hpp"
#include <map>
#include <string>
#include <vector>

namespace heel::formal
{
struct FieldSpec
{
    std::string id, label;
    int section;
};
struct TableSpec
{
    std::string id, title;
    int section;
    std::vector<std::string> columns;
};
struct Attachment
{
    // A composite file can explicitly cover several logical parts, separated by '+',
    // e.g. "tape_calibration+tape_calibration_back". No coverage is inferred from a scan.
    std::string slot, path, caption;
};
struct Data
{
    std::map<std::string, std::string> fields;
    std::map<std::string, std::vector<std::vector<std::string>>> tables;
    std::vector<Attachment> attachments;
};
const std::vector<FieldSpec> &FieldSpecs();
const std::vector<TableSpec> &TableSpecs();
Data DefaultData();
std::string ToJson(const Data &);
bool FromJson(const json::Value &, Data &, std::string *err = nullptr);
} // namespace heel::formal

#include "IncliningProtocolSchema.hpp"
