#pragma once
#include <QString>
#include <functional>
#include <nlohmann/json.hpp>

// DEC-0092: offline only. Requires a new output directory; never overwrites data.
// Runs the pinned helper with bounded input, output and execution time.
nlohmann::json decodeSstvImageFile(const QString& input, const QString& outputDirectory,
                                 const QString& mode = "auto",
                                 const std::function<bool()>& cancelled = {});
