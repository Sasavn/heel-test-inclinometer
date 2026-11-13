#pragma once
#include "IncliningProtocolData.hpp"
#include <filesystem>

namespace heel
{
struct Settings;
struct RsCase;
struct ReportInfo;
struct ReportImage;
namespace formal
{
inline constexpr const char *kDocxName = "Протокол_кренования.docx";
bool WriteDocx(const std::filesystem::path &, const Data &, const Settings &, const RsCase &, const ReportInfo &,
               const std::vector<ReportImage> &, std::string *err = nullptr);
} // namespace formal
} // namespace heel
