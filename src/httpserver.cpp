#include "httpserver.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <atomic>
#include <unordered_map>
#include "datamanager.h"
#include "densityanalysis.h"
#include "frequentpathmanager.h"
#include "httplib.h"
#include "json.hpp"
#include "logger.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
struct DensityBucketSummary {
    long long startTime = 0;
    long long endTime = 0;
    int nonZeroCount = 0;
    double maxDensity = 0.0;
};

struct DensityCacheEntry {
    std::string queryId;
    DensityAnalysisRequest request;
    DensityAnalysisResult result;
    std::vector<DensityBucketSummary> bucketSummaries;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point lastAccessAt;
};

std::mutex g_densityCacheMutex;
std::unordered_map<std::string, std::shared_ptr<DensityCacheEntry>> g_densityCacheById;

// 为了简单起见，先只保留很少的缓存结果，避免本地内存继续炸
constexpr std::size_t kDensityCacheMaxEntries = 2;
constexpr auto kDensityCacheTtl = std::chrono::minutes(15);

std::string makeDensityRequestKey(const DensityAnalysisRequest& request) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8)
        << request.minLon << '|'
        << request.minLat << '|'
        << request.maxLon << '|'
        << request.maxLat << '|'
        << request.startTime << '|'
        << request.endTime << '|'
        << request.intervalMinutes << '|'
        << request.cellSizeMeters;
    return oss.str();
}

std::string makeDensityQueryId() {
    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    static std::atomic<unsigned long long> seq{0};
    const auto id = ++seq;

    std::ostringstream oss;
    oss << "density_" << nowNs << "_" << id;
    return oss.str();
}

void cleanupDensityCacheLocked() {
    const auto now = std::chrono::steady_clock::now();

    for (auto it = g_densityCacheById.begin(); it != g_densityCacheById.end(); ) {
        const auto& entry = it->second;
        if (!entry || (now - entry->lastAccessAt) > kDensityCacheTtl) {
            it = g_densityCacheById.erase(it);
        } else {
            ++it;
        }
    }

    if (g_densityCacheById.size() <= kDensityCacheMaxEntries) {
        return;
    }

    std::vector<std::pair<std::string, std::shared_ptr<DensityCacheEntry>>> entries;
    entries.reserve(g_densityCacheById.size());
    for (const auto& kv : g_densityCacheById) {
        entries.push_back(kv);
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.second->lastAccessAt < b.second->lastAccessAt;
              });

    while (g_densityCacheById.size() > kDensityCacheMaxEntries && !entries.empty()) {
        g_densityCacheById.erase(entries.front().first);
        entries.erase(entries.begin());
    }
}

std::shared_ptr<DensityCacheEntry> findDensityCacheBySameRequestLocked(const DensityAnalysisRequest& request) {
    const std::string targetKey = makeDensityRequestKey(request);

    for (auto& kv : g_densityCacheById) {
        auto& entry = kv.second;
        if (!entry) {
            continue;
        }
        if (makeDensityRequestKey(entry->request) == targetKey) {
            entry->lastAccessAt = std::chrono::steady_clock::now();
            return entry;
        }
    }
    return nullptr;
}

std::vector<DensityBucketSummary> buildDensityBucketSummaries(
    const DensityAnalysisRequest& request,
    const DensityAnalysisResult& result) {

    std::vector<DensityBucketSummary> summaries;
    summaries.resize(static_cast<std::size_t>(result.bucketCount));

    const int bucketCount = result.bucketCount;
    const int columnCount = result.columnCount;
    const int rowCount = result.rowCount;
    const int gridSize = columnCount * rowCount;
    const int bucketSeconds = request.intervalMinutes * 60;
    const double bucketSecondsDouble = std::max(1.0, static_cast<double>(bucketSeconds));
    const double invArea = 1.0 / std::max(1e-9, result.cellAreaKm2);

    for (int b = 0; b < bucketCount; ++b) {
        auto& summary = summaries[static_cast<std::size_t>(b)];
        summary.startTime = request.startTime + static_cast<long long>(b) * bucketSeconds;
        summary.endTime = std::min(summary.startTime + bucketSeconds - 1LL, request.endTime);

        const int base = b * gridSize;
        int nonZeroCount = 0;
        double maxDensity = 0.0;

        for (int i = 0; i < gridSize; ++i) {
            const float seconds = result.vehicleSeconds[base + i];
            if (seconds <= 0.0f) {
                continue;
            }

            ++nonZeroCount;
            const double density = (static_cast<double>(seconds) / bucketSecondsDouble) * invArea;
            if (density > maxDensity) {
                maxDensity = density;
            }
        }

        summary.nonZeroCount = nonZeroCount;
        summary.maxDensity = maxDensity;
    }

    return summaries;
}

std::shared_ptr<DensityCacheEntry> getOrCreateDensityCacheEntry(const DensityAnalysisRequest& request) {
    {
        std::lock_guard<std::mutex> lock(g_densityCacheMutex);
        cleanupDensityCacheLocked();
        if (auto existing = findDensityCacheBySameRequestLocked(request)) {
            Debug() << "[density-cache] hit, queryId=" << existing->queryId << std::endl;
            return existing;
        }
    }

    Debug() << "[density-cache] miss, analyze begin" << std::endl;
    const DensityAnalysisResult result = DensityAnalyzer::analyze(request);
    if (!result.success) {
        return nullptr;
    }

    auto entry = std::make_shared<DensityCacheEntry>();
    entry->queryId = makeDensityQueryId();
    entry->request = request;
    entry->result = result;
    entry->bucketSummaries = buildDensityBucketSummaries(request, result);
    entry->createdAt = std::chrono::steady_clock::now();
    entry->lastAccessAt = entry->createdAt;

    {
        std::lock_guard<std::mutex> lock(g_densityCacheMutex);
        cleanupDensityCacheLocked();
        g_densityCacheById[entry->queryId] = entry;
    }

    Debug() << "[density-cache] store ok, queryId=" << entry->queryId
            << ", vehicleSeconds bytes≈"
            << static_cast<long long>(entry->result.vehicleSeconds.size() * sizeof(float))
            << std::endl;

    return entry;
}

std::shared_ptr<DensityCacheEntry> getDensityCacheById(const std::string& queryId) {
    std::lock_guard<std::mutex> lock(g_densityCacheMutex);
    cleanupDensityCacheLocked();

    auto it = g_densityCacheById.find(queryId);
    if (it == g_densityCacheById.end() || !it->second) {
        return nullptr;
    }

    it->second->lastAccessAt = std::chrono::steady_clock::now();
    return it->second;
}

bool tryReadBucketIndex(const json& body, int& bucketIndex) {
    if (!body.contains("bucketIndex")) {
        return false;
    }
    const json& value = body.at("bucketIndex");
    if (!value.is_number_integer()) {
        return false;
    }
    bucketIndex = value.get<int>();
    return true;
}

bool tryReadGridIndex(const json& body, const char* key, int& out) {
    if (!body.contains(key)) {
        return false;
    }
    const json& value = body.at(key);
    if (!value.is_number_integer()) {
        return false;
    }
    out = value.get<int>();
    return true;
}
json makeSuccess(const json& data) {
    return json{{"success", true}, {"data", data}};
}

json makeError(const std::string& code, const std::string& message) {
    return json{
        {"success", false},
        {"error", {{"code", code}, {"message", message}}}
    };
}

bool parseJsonBody(const httplib::Request& req, json& out, std::string& errorMessage) {
    try {
        out = json::parse(req.body);
        if (!out.is_object()) {
            errorMessage = "request body must be a JSON object";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        errorMessage = ex.what();
        return false;
    }
}

void applyCorsHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Max-Age", "86400");
}

json pointToJson(const GPSPoint& point) {
    return json{
        {"id", point.id},
        {"timestamp", point.timestamp},
        {"lon", point.lon},
        {"lat", point.lat}
    };
}

long long toInt64(const json& value, long long defaultValue = 0) {
    if (value.is_number_integer()) {
        return value.get<long long>();
    }
    if (value.is_string()) {
        try {
            return std::stoll(value.get<std::string>());
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

bool tryReadDouble(const json& body, const char* key, double& out) {
    if (!body.contains(key)) {
        return false;
    }
    const json& value = body.at(key);
    if (!value.is_number()) {
        return false;
    }
    out = value.get<double>();
    return std::isfinite(out);
}

bool tryReadRegionBounds(const json& body,
                         double& minLon,
                         double& minLat,
                         double& maxLon,
                         double& maxLat) {
    const bool hasMinLon = tryReadDouble(body, "minLon", minLon);
    const bool hasMinLat = tryReadDouble(body, "minLat", minLat);
    const bool hasMaxLon = tryReadDouble(body, "maxLon", maxLon);
    const bool hasMaxLat = tryReadDouble(body, "maxLat", maxLat);
    if (!(hasMinLon && hasMinLat && hasMaxLon && hasMaxLat)) {
        return false;
    }
    return minLon < maxLon && minLat < maxLat;
}

std::string readTextFile(const fs::path& path) {
    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open()) {
        return "";
    }
    std::ostringstream oss;
    oss << fin.rdbuf();
    return oss.str();
}

constexpr const char* kArrowStreamContentType = "application/vnd.apache.arrow.stream";

enum class ArrowPrimitiveType {
    Int32,
    Int64,
    Float64
};

struct ArrowFieldSpec {
    std::string name;
    ArrowPrimitiveType type;
};

struct ArrowBufferRange {
    std::int64_t offset = 0;
    std::int64_t length = 0;
};

struct DensityBucketArrowRow {
    std::int32_t gx = 0;
    std::int32_t gy = 0;
    double seconds = 0.0;
};

struct DensityTrendArrowRow {
    std::int32_t bucketIndex = 0;
    std::int64_t startTime = 0;
    std::int64_t endTime = 0;
    double seconds = 0.0;
};

template <typename T>
void appendScalarLE(std::vector<std::uint8_t>& out, T value) {
    const std::size_t start = out.size();
    out.resize(start + sizeof(T));
    std::memcpy(out.data() + start, &value, sizeof(T));
}

template <typename T>
void writeScalarLEAt(std::vector<std::uint8_t>& out, std::size_t offset, T value) {
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

void alignBuffer(std::vector<std::uint8_t>& out, std::size_t alignment) {
    if (alignment == 0) {
        return;
    }
    const std::size_t remain = out.size() % alignment;
    if (remain == 0) {
        return;
    }
    out.resize(out.size() + (alignment - remain), 0);
}

void patchUOffset(std::vector<std::uint8_t>& out, std::size_t slotPos, std::size_t targetPos) {
    const std::uint32_t offset = static_cast<std::uint32_t>(targetPos - slotPos);
    writeScalarLEAt<std::uint32_t>(out, slotPos, offset);
}

class FlatbufferTableBuilder {
public:
    FlatbufferTableBuilder(std::vector<std::uint8_t>& out, std::uint16_t fieldCount)
        : out_(out), fieldCount_(fieldCount), present_(fieldCount, false) {
        alignBuffer(out_, 8);
        start_ = out_.size();
        appendScalarLE<std::int32_t>(out_, 0); // soffset to vtable
        out_.resize(out_.size() + static_cast<std::size_t>(fieldCount_) * kSlotStride, 0);
    }

    std::size_t setUOffsetField(std::uint16_t fieldIndex) {
        present_[fieldIndex] = true;
        return slotPos(fieldIndex);
    }

    void setBoolField(std::uint16_t fieldIndex, bool value) {
        present_[fieldIndex] = true;
        writeScalarLEAt<std::uint8_t>(out_, slotPos(fieldIndex), value ? 1u : 0u);
    }

    void setU8Field(std::uint16_t fieldIndex, std::uint8_t value) {
        present_[fieldIndex] = true;
        writeScalarLEAt<std::uint8_t>(out_, slotPos(fieldIndex), value);
    }

    void setI16Field(std::uint16_t fieldIndex, std::int16_t value) {
        present_[fieldIndex] = true;
        writeScalarLEAt<std::int16_t>(out_, slotPos(fieldIndex), value);
    }

    void setI32Field(std::uint16_t fieldIndex, std::int32_t value) {
        present_[fieldIndex] = true;
        writeScalarLEAt<std::int32_t>(out_, slotPos(fieldIndex), value);
    }

    void setI64Field(std::uint16_t fieldIndex, std::int64_t value) {
        present_[fieldIndex] = true;
        writeScalarLEAt<std::int64_t>(out_, slotPos(fieldIndex), value);
    }

    std::size_t finish() {
        alignBuffer(out_, 2);
        const std::size_t vtableStart = out_.size();
        const std::uint16_t vtableSize = static_cast<std::uint16_t>(4 + fieldCount_ * 2);
        const std::uint16_t objectSize = static_cast<std::uint16_t>(4 + fieldCount_ * kSlotStride);

        appendScalarLE<std::uint16_t>(out_, vtableSize);
        appendScalarLE<std::uint16_t>(out_, objectSize);
        for (std::uint16_t i = 0; i < fieldCount_; ++i) {
            const std::uint16_t fieldOffset = present_[i]
                ? static_cast<std::uint16_t>(4 + i * kSlotStride)
                : 0;
            appendScalarLE<std::uint16_t>(out_, fieldOffset);
        }

        const std::int32_t soffset = static_cast<std::int32_t>(start_ - vtableStart);
        writeScalarLEAt<std::int32_t>(out_, start_, soffset);
        return start_;
    }

private:
    std::size_t slotPos(std::uint16_t fieldIndex) const {
        return start_ + 4 + static_cast<std::size_t>(fieldIndex) * kSlotStride;
    }

private:
    static constexpr std::uint16_t kSlotStride = 8;

    std::vector<std::uint8_t>& out_;
    std::uint16_t fieldCount_;
    std::size_t start_{0};
    std::vector<bool> present_;
};

std::size_t addString(std::vector<std::uint8_t>& out, const std::string& value) {
    alignBuffer(out, 4);
    const std::size_t start = out.size();
    appendScalarLE<std::uint32_t>(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0);
    return start;
}

std::size_t addEmptyOffsetVector(std::vector<std::uint8_t>& out) {
    alignBuffer(out, 4);
    const std::size_t start = out.size();
    appendScalarLE<std::uint32_t>(out, 0);
    return start;
}

template <typename BuildElementFn>
std::size_t addOffsetVector(std::vector<std::uint8_t>& out,
                            std::size_t count,
                            const BuildElementFn& buildElement) {
    alignBuffer(out, 4);
    const std::size_t start = out.size();
    appendScalarLE<std::uint32_t>(out, static_cast<std::uint32_t>(count));

    std::vector<std::size_t> slots;
    slots.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        slots.push_back(out.size());
        appendScalarLE<std::uint32_t>(out, 0);
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t targetPos = buildElement(i);
        patchUOffset(out, slots[i], targetPos);
    }
    return start;
}

std::size_t addFieldNodeVector(std::vector<std::uint8_t>& out, std::int64_t rowCount, std::size_t fieldCount) {
    alignBuffer(out, 8);
    const std::size_t start = out.size();
    appendScalarLE<std::uint32_t>(out, static_cast<std::uint32_t>(fieldCount));
    for (std::size_t i = 0; i < fieldCount; ++i) {
        appendScalarLE<std::int64_t>(out, rowCount);
        appendScalarLE<std::int64_t>(out, 0);
    }
    return start;
}

std::size_t addBufferVector(std::vector<std::uint8_t>& out, const std::vector<ArrowBufferRange>& buffers) {
    alignBuffer(out, 8);
    const std::size_t start = out.size();
    appendScalarLE<std::uint32_t>(out, static_cast<std::uint32_t>(buffers.size()));
    for (const auto& buffer : buffers) {
        appendScalarLE<std::int64_t>(out, buffer.offset);
        appendScalarLE<std::int64_t>(out, buffer.length);
    }
    return start;
}

std::size_t buildIntTypeTable(std::vector<std::uint8_t>& out, std::int32_t bitWidth) {
    FlatbufferTableBuilder table(out, 2);
    table.setI32Field(0, bitWidth);
    table.setBoolField(1, true);
    return table.finish();
}

std::size_t buildFloatingPointTypeTable(std::vector<std::uint8_t>& out) {
    FlatbufferTableBuilder table(out, 1);
    table.setI16Field(0, 2); // DOUBLE
    return table.finish();
}

std::uint8_t getArrowTypeUnionCode(ArrowPrimitiveType type) {
    switch (type) {
        case ArrowPrimitiveType::Float64:
            return 3; // FloatingPoint
        case ArrowPrimitiveType::Int32:
        case ArrowPrimitiveType::Int64:
            return 2; // Int
    }
    return 0;
}

std::size_t buildFieldTable(std::vector<std::uint8_t>& out, const ArrowFieldSpec& field) {
    FlatbufferTableBuilder table(out, 7);
    const std::size_t nameSlot = table.setUOffsetField(0);
    table.setBoolField(1, true);
    table.setU8Field(2, getArrowTypeUnionCode(field.type));
    const std::size_t typeSlot = table.setUOffsetField(3);
    const std::size_t childrenSlot = table.setUOffsetField(5);
    const std::size_t tablePos = table.finish();

    const std::size_t namePos = addString(out, field.name);
    patchUOffset(out, nameSlot, namePos);

    std::size_t typePos = 0;
    if (field.type == ArrowPrimitiveType::Float64) {
        typePos = buildFloatingPointTypeTable(out);
    } else if (field.type == ArrowPrimitiveType::Int64) {
        typePos = buildIntTypeTable(out, 64);
    } else {
        typePos = buildIntTypeTable(out, 32);
    }
    patchUOffset(out, typeSlot, typePos);

    const std::size_t childrenPos = addEmptyOffsetVector(out);
    patchUOffset(out, childrenSlot, childrenPos);

    return tablePos;
}

std::size_t buildSchemaTable(std::vector<std::uint8_t>& out, const std::vector<ArrowFieldSpec>& fields) {
    FlatbufferTableBuilder table(out, 4);
    table.setI16Field(0, 0); // Little-endian
    const std::size_t fieldsSlot = table.setUOffsetField(1);
    const std::size_t tablePos = table.finish();

    const std::size_t fieldsVecPos = addOffsetVector(out, fields.size(), [&](std::size_t i) {
        return buildFieldTable(out, fields[i]);
    });
    patchUOffset(out, fieldsSlot, fieldsVecPos);
    return tablePos;
}

std::size_t buildRecordBatchTable(std::vector<std::uint8_t>& out,
                                  std::int64_t rowCount,
                                  const std::vector<ArrowBufferRange>& buffers,
                                  std::size_t fieldCount) {
    FlatbufferTableBuilder table(out, 5);
    table.setI64Field(0, rowCount);
    const std::size_t nodesSlot = table.setUOffsetField(1);
    const std::size_t buffersSlot = table.setUOffsetField(2);
    const std::size_t tablePos = table.finish();

    const std::size_t nodesPos = addFieldNodeVector(out, rowCount, fieldCount);
    patchUOffset(out, nodesSlot, nodesPos);

    const std::size_t buffersPos = addBufferVector(out, buffers);
    patchUOffset(out, buffersSlot, buffersPos);

    return tablePos;
}

template <typename BuildHeaderFn>
std::vector<std::uint8_t> buildArrowMessageMetadata(std::uint8_t headerType,
                                                    std::int64_t bodyLength,
                                                    const BuildHeaderFn& buildHeader) {
    std::vector<std::uint8_t> out;
    out.reserve(512);
    appendScalarLE<std::uint32_t>(out, 0); // root uoffset

    FlatbufferTableBuilder message(out, 5);
    message.setI16Field(0, 4); // MetadataVersion::V5
    message.setU8Field(1, headerType);
    const std::size_t headerSlot = message.setUOffsetField(2);
    message.setI64Field(3, bodyLength);
    const std::size_t messagePos = message.finish();

    const std::size_t headerPos = buildHeader(out);
    patchUOffset(out, headerSlot, headerPos);

    writeScalarLEAt<std::uint32_t>(out, 0, static_cast<std::uint32_t>(messagePos));
    return out;
}

std::vector<std::uint8_t> buildSchemaMessageMetadata(const std::vector<ArrowFieldSpec>& fields) {
    return buildArrowMessageMetadata(1, 0, [&](std::vector<std::uint8_t>& out) {
        return buildSchemaTable(out, fields);
    });
}

std::vector<std::uint8_t> buildRecordBatchMessageMetadata(std::int64_t rowCount,
                                                          const std::vector<ArrowBufferRange>& buffers,
                                                          std::size_t fieldCount,
                                                          std::int64_t bodyLength) {
    return buildArrowMessageMetadata(3, bodyLength, [&](std::vector<std::uint8_t>& out) {
        return buildRecordBatchTable(out, rowCount, buffers, fieldCount);
    });
}

void appendArrowStreamMessage(std::vector<std::uint8_t>& out,
                              const std::vector<std::uint8_t>& metadata,
                              const std::vector<std::uint8_t>& body) {
    const std::size_t paddedMetadataSize = ((metadata.size() + 7u) / 8u) * 8u;
    appendScalarLE<std::int32_t>(out, -1);
    appendScalarLE<std::int32_t>(out, static_cast<std::int32_t>(paddedMetadataSize));
    out.insert(out.end(), metadata.begin(), metadata.end());
    while ((out.size() % 8u) != 0u) {
        out.push_back(0);
    }
    out.insert(out.end(), body.begin(), body.end());
    while ((out.size() % 8u) != 0u) {
        out.push_back(0);
    }
}

std::string buildArrowStream(const std::vector<ArrowFieldSpec>& fields,
                             const std::vector<ArrowBufferRange>& buffers,
                             std::vector<std::uint8_t> body,
                             std::int64_t rowCount) {
    alignBuffer(body, 8);
    const std::int64_t bodyLength = static_cast<std::int64_t>(body.size());

    const std::vector<std::uint8_t> schemaMeta = buildSchemaMessageMetadata(fields);
    const std::vector<std::uint8_t> recordBatchMeta = buildRecordBatchMessageMetadata(
        rowCount, buffers, fields.size(), bodyLength
    );

    std::vector<std::uint8_t> stream;
    stream.reserve(schemaMeta.size() + recordBatchMeta.size() + body.size() + 96);
    appendArrowStreamMessage(stream, schemaMeta, {});
    appendArrowStreamMessage(stream, recordBatchMeta, body);
    appendScalarLE<std::int32_t>(stream, -1);
    appendScalarLE<std::int32_t>(stream, 0);

    return std::string(reinterpret_cast<const char*>(stream.data()), stream.size());
}

std::string buildDensityBucketArrowStream(const DensityCacheEntry& entry, int bucketIndex) {
    const int columnCount = entry.result.columnCount;
    const int rowCount = entry.result.rowCount;
    const int gridSize = columnCount * rowCount;
    const int base = bucketIndex * gridSize;

    std::vector<DensityBucketArrowRow> rows;
    rows.reserve(static_cast<std::size_t>(entry.bucketSummaries[static_cast<std::size_t>(bucketIndex)].nonZeroCount));

    for (int i = 0; i < gridSize; ++i) {
        const float seconds = entry.result.vehicleSeconds[base + i];
        if (seconds <= 0.0f) {
            continue;
        }
        rows.push_back(DensityBucketArrowRow{
            static_cast<std::int32_t>(i % columnCount),
            static_cast<std::int32_t>(i / columnCount),
            static_cast<double>(seconds)
        });
    }

    std::vector<std::uint8_t> body;
    body.reserve(rows.size() * 24);
    std::vector<ArrowBufferRange> buffers;
    buffers.reserve(6);

    alignBuffer(body, 8);
    const std::int64_t gxOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({gxOffset, 0});
    buffers.push_back({gxOffset, static_cast<std::int64_t>(rows.size() * sizeof(std::int32_t))});
    for (const auto& row : rows) {
        appendScalarLE<std::int32_t>(body, row.gx);
    }

    alignBuffer(body, 8);
    const std::int64_t gyOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({gyOffset, 0});
    buffers.push_back({gyOffset, static_cast<std::int64_t>(rows.size() * sizeof(std::int32_t))});
    for (const auto& row : rows) {
        appendScalarLE<std::int32_t>(body, row.gy);
    }

    alignBuffer(body, 8);
    const std::int64_t secondsOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({secondsOffset, 0});
    buffers.push_back({secondsOffset, static_cast<std::int64_t>(rows.size() * sizeof(double))});
    for (const auto& row : rows) {
        appendScalarLE<double>(body, row.seconds);
    }

    const std::vector<ArrowFieldSpec> fields = {
        {"gx", ArrowPrimitiveType::Int32},
        {"gy", ArrowPrimitiveType::Int32},
        {"seconds", ArrowPrimitiveType::Float64}
    };
    return buildArrowStream(fields, buffers, std::move(body), static_cast<std::int64_t>(rows.size()));
}

std::string buildDensityTrendArrowStream(const DensityCacheEntry& entry, int gx, int gy) {
    const int bucketCount = entry.result.bucketCount;
    const int columnCount = entry.result.columnCount;
    const int rowCount = entry.result.rowCount;

    std::vector<DensityTrendArrowRow> rows;
    rows.reserve(static_cast<std::size_t>(bucketCount));

    for (int b = 0; b < bucketCount; ++b) {
        const std::size_t idx =
            static_cast<std::size_t>(b) * static_cast<std::size_t>(columnCount) * static_cast<std::size_t>(rowCount) +
            static_cast<std::size_t>(gy) * static_cast<std::size_t>(columnCount) +
            static_cast<std::size_t>(gx);
        const auto& summary = entry.bucketSummaries[static_cast<std::size_t>(b)];

        rows.push_back(DensityTrendArrowRow{
            static_cast<std::int32_t>(b),
            static_cast<std::int64_t>(summary.startTime),
            static_cast<std::int64_t>(summary.endTime),
            static_cast<double>(entry.result.vehicleSeconds[idx])
        });
    }

    std::vector<std::uint8_t> body;
    body.reserve(rows.size() * 40);
    std::vector<ArrowBufferRange> buffers;
    buffers.reserve(8);

    alignBuffer(body, 8);
    const std::int64_t bucketIndexOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({bucketIndexOffset, 0});
    buffers.push_back({bucketIndexOffset, static_cast<std::int64_t>(rows.size() * sizeof(std::int32_t))});
    for (const auto& row : rows) {
        appendScalarLE<std::int32_t>(body, row.bucketIndex);
    }

    alignBuffer(body, 8);
    const std::int64_t startTimeOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({startTimeOffset, 0});
    buffers.push_back({startTimeOffset, static_cast<std::int64_t>(rows.size() * sizeof(std::int64_t))});
    for (const auto& row : rows) {
        appendScalarLE<std::int64_t>(body, row.startTime);
    }

    alignBuffer(body, 8);
    const std::int64_t endTimeOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({endTimeOffset, 0});
    buffers.push_back({endTimeOffset, static_cast<std::int64_t>(rows.size() * sizeof(std::int64_t))});
    for (const auto& row : rows) {
        appendScalarLE<std::int64_t>(body, row.endTime);
    }

    alignBuffer(body, 8);
    const std::int64_t secondsOffset = static_cast<std::int64_t>(body.size());
    buffers.push_back({secondsOffset, 0});
    buffers.push_back({secondsOffset, static_cast<std::int64_t>(rows.size() * sizeof(double))});
    for (const auto& row : rows) {
        appendScalarLE<double>(body, row.seconds);
    }

    const std::vector<ArrowFieldSpec> fields = {
        {"bucketIndex", ArrowPrimitiveType::Int32},
        {"startTime", ArrowPrimitiveType::Int64},
        {"endTime", ArrowPrimitiveType::Int64},
        {"seconds", ArrowPrimitiveType::Float64}
    };
    return buildArrowStream(fields, buffers, std::move(body), static_cast<std::int64_t>(rows.size()));
}

} // namespace

HttpServer::HttpServer(const AppConfig& config)
    : m_config(config) {}

std::string HttpServer::resolveWebRoot() const {
    std::vector<fs::path> roots;

#ifdef APP_SOURCE_WEB_ROOT
    roots.emplace_back(APP_SOURCE_WEB_ROOT);
#endif

    if (!m_config.mapPath.empty()) {
        fs::path configuredPath(m_config.mapPath);
        if (fs::is_regular_file(configuredPath)) {
            roots.push_back(configuredPath.parent_path());
        } else {
            roots.push_back(configuredPath);
        }
    }

    roots.push_back(fs::current_path() / "web");
    roots.push_back(fs::current_path() / "../web");
    roots.push_back(fs::current_path() / "../../web");

    for (const auto& root : roots) {
        const fs::path indexPath = root / "index.html";
        if (fs::exists(indexPath)) {
            return fs::absolute(root).lexically_normal().string();
        }
    }

    if (!m_config.mapPath.empty()) {
        fs::path configuredPath(m_config.mapPath);
        return fs::is_regular_file(configuredPath)
                   ? configuredPath.parent_path().lexically_normal().string()
                   : configuredPath.lexically_normal().string();
    }

    return fs::absolute(fs::current_path() / "../web").lexically_normal().string();
}

bool HttpServer::start(std::uint16_t port) {
    httplib::Server server;
    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Headers", "Content-Type"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Max-Age", "86400"},
        {"Cache-Control", "no-store"}
    });

    server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        applyCorsHeaders(res);
        res.status = 204;
    });

    const std::string webRoot = resolveWebRoot();
    Debug() << "Web root: " << webRoot << std::endl;

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        const json data = {
            {"status", "ok"},
            {"pointsLoaded", static_cast<long long>(DataManager::getAllPoints().size())}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Get("/api/meta", [this](const httplib::Request&, httplib::Response& res) {
        const json data = {
            {"minLon", m_config.minLon},
            {"maxLon", m_config.maxLon},
            {"minLat", m_config.minLat},
            {"maxLat", m_config.maxLat},
            {"centerLon", m_config.mapCenterLon},
            {"centerLat", m_config.mapCenterLat},
            {"initialZoom", m_config.mapInitialZoom},
            {"minZoom", m_config.mapMinZoom},
            {"maxZoom", m_config.mapMaxZoom},
            {"totalPoints", static_cast<long long>(DataManager::getAllPoints().size())},
            {"baiduMapAk", m_config.baiduMapAk}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/trajectory", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        std::string errorMessage;
        if (!parseJsonBody(req, body, errorMessage)) {
            res.status = 400;
            res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
            return;
        }

        const long long taxiId = body.value("taxiId", -1LL);
        if (taxiId < 0) {
            res.status = 400;
            res.set_content(makeError("INVALID_ARGUMENT", "taxiId invalid").dump(), "application/json; charset=utf-8");
            return;
        }

        if (taxiId == 0) {
            const bool hasBounds = body.contains("minLon") && body.contains("minLat") &&
                                   body.contains("maxLon") && body.contains("maxLat");
            if (!hasBounds) {
                res.status = 400;
                res.set_content(makeError("INVALID_ARGUMENT", "all-taxi query requires map bounds").dump(), "application/json; charset=utf-8");
                return;
            }

            const double minLon = body.value("minLon", 0.0);
            const double minLat = body.value("minLat", 0.0);
            const double maxLon = body.value("maxLon", 0.0);
            const double maxLat = body.value("maxLat", 0.0);
            const int zoom = body.value("zoom", 12);

            if (minLon >= maxLon || minLat >= maxLat) {
                res.status = 400;
                res.set_content(makeError("INVALID_ARGUMENT", "bounds invalid").dump(), "application/json; charset=utf-8");
                return;
            }

            const std::vector<GPSPoint> points = DataManager::querySpatial(minLon, minLat, maxLon, maxLat);
            const bool canRenderRaw = zoom >= 18 && points.size() <= 12000;
            if (canRenderRaw) {
                json pointArray = json::array();
                for (const auto& point : points) {
                    pointArray.push_back(pointToJson(point));
                }

                const json data = {
                    {"taxiId", 0},
                    {"mode", "raw"},
                    {"pointCount", static_cast<long long>(points.size())},
                    {"clusterCount", static_cast<long long>(points.size())},
                    {"renderCap", 12000},
                    {"points", pointArray}
                };
                res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
                return;
            }

            const std::vector<ClusterPoint> clusters =
                DataManager::clusterPointsForView(points, minLon, minLat, maxLon, maxLat, zoom);

            json clusterArray = json::array();
            for (const auto& cluster : clusters) {
                clusterArray.push_back({
                    {"lng", cluster.lon},
                    {"lat", cluster.lat},
                    {"count", cluster.count},
                    {"isCluster", cluster.isCluster},
                    {"minLon", cluster.minLon},
                    {"minLat", cluster.minLat},
                    {"maxLon", cluster.maxLon},
                    {"maxLat", cluster.maxLat}
                });
            }

            const json data = {
                {"taxiId", 0},
                {"mode", "cluster"},
                {"pointCount", static_cast<long long>(points.size())},
                {"clusterCount", static_cast<long long>(clusters.size())},
                {"renderCap", 12000},
                {"points", clusterArray}
            };
            res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
            return;
        }

        const std::vector<GPSPoint> points = DataManager::getPointsRangeById(static_cast<int>(taxiId));
        json pointArray = json::array();
        for (const auto& point : points) {
            pointArray.push_back(pointToJson(point));
        }

        const json data = {
            {"taxiId", taxiId},
            {"mode", "trajectory"},
            {"pointCount", static_cast<long long>(points.size())},
            {"points", pointArray}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/region-search", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        std::string errorMessage;
        if (!parseJsonBody(req, body, errorMessage)) {
            res.status = 400;
            res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
            return;
        }

        const double minLon = body.value("minLon", 0.0);
        const double minLat = body.value("minLat", 0.0);
        const double maxLon = body.value("maxLon", 0.0);
        const double maxLat = body.value("maxLat", 0.0);
        const long long startTime = toInt64(body.value("startTime", 0), 0);
        const long long endTime = toInt64(body.value("endTime", 0), 0);

        if (minLon >= maxLon || minLat >= maxLat || startTime > endTime) {
            res.status = 400;
            res.set_content(makeError("INVALID_ARGUMENT", "region or time range invalid").dump(), "application/json; charset=utf-8");
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        const std::vector<GPSPoint> points = DataManager::querySpatialAndTime(minLon, minLat, maxLon, maxLat, startTime, endTime);
        const int vehicleCount = DataManager::getUniqueCountById(points);
        const double elapsedSeconds = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count()) / 1000.0;

        const json data = {
            {"pointCount", static_cast<long long>(points.size())},
            {"vehicleCount", vehicleCount},
            {"elapsedSeconds", elapsedSeconds}
        };
        res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
    });

    server.Post("/api/density/meta", [this](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();
    
    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-meta] FAILED (parse error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    DensityAnalysisRequest request;
    request.minLon = m_config.minLon;
    request.minLat = m_config.minLat;
    request.maxLon = m_config.maxLon;
    request.maxLat = m_config.maxLat;

    double regionMinLon = 0.0;
    double regionMinLat = 0.0;
    double regionMaxLon = 0.0;
    double regionMaxLat = 0.0;
    const bool useClientRegion = tryReadRegionBounds(body, regionMinLon, regionMinLat, regionMaxLon, regionMaxLat);
    if (body.contains("minLon") || body.contains("minLat") || body.contains("maxLon") || body.contains("maxLat")) {
        if (!useClientRegion) {
            res.status = 400;
            res.set_content(makeError("INVALID_ARGUMENT", "density region bounds invalid").dump(), "application/json; charset=utf-8");
            return;
        }
        request.minLon = regionMinLon;
        request.minLat = regionMinLat;
        request.maxLon = regionMaxLon;
        request.maxLat = regionMaxLat;
    }

    request.startTime = toInt64(body.value("startTime", 0), 0);
    request.endTime = toInt64(body.value("endTime", 0), 0);
    request.intervalMinutes = body.value("intervalMinutes", 30);
    request.cellSizeMeters = body.value("cellSizeMeters", 500.0);

    if (request.minLon >= request.maxLon || request.minLat >= request.maxLat) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "map bounds invalid").dump(), "application/json; charset=utf-8");
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto entry = getOrCreateDensityCacheEntry(request);
    const auto t1 = std::chrono::steady_clock::now();
    const auto cacheFetchMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (!entry) {
        res.status = 400;
        res.set_content(makeError("ANALYSIS_FAILED", "density analyze failed").dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-meta] FAILED (analysis error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    json bucketSummaryArray = json::array();
    for (const auto& bucket : entry->bucketSummaries) {
        bucketSummaryArray.push_back({
            {"startTime", bucket.startTime},
            {"endTime", bucket.endTime},
            {"nonZeroCount", bucket.nonZeroCount},
            {"maxDensity", bucket.maxDensity}
        });
    }

    const json data = {
        {"queryId", entry->queryId},
        {"minLon", entry->request.minLon},
        {"minLat", entry->request.minLat},
        {"maxLon", entry->request.maxLon},
        {"maxLat", entry->request.maxLat},
        {"regionSource", useClientRegion ? "selection" : "full-map"},
        {"startTime", entry->request.startTime},
        {"endTime", entry->request.endTime},
        {"intervalMinutes", entry->request.intervalMinutes},
        {"bucketSeconds", entry->request.intervalMinutes * 60},
        {"cellSizeMeters", entry->request.cellSizeMeters},
        {"lonStep", entry->result.lonStep},
        {"latStep", entry->result.latStep},
        {"cellAreaKm2", entry->result.cellAreaKm2},
        {"columnCount", entry->result.columnCount},
        {"rowCount", entry->result.rowCount},
        {"bucketCount", entry->result.bucketCount},
        {"gridCount", entry->result.gridCount},
        {"analysisScale", entry->result.analysisScale},
        {"maxVehicleDensity", entry->result.maxVehicleDensity},
        {"totalPointCount", entry->result.totalPointCount},
        {"totalVehicleCount", entry->result.totalVehicleCount},
        {"elapsedSeconds", entry->result.elapsedSeconds},
        {"cacheFetchCostMs", cacheFetchMs},
        {"buckets", bucketSummaryArray}
    };

    const auto t_end = std::chrono::steady_clock::now();
    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    Debug() << "[density-meta] " << totalMs << "ms (grid:" << entry->result.columnCount 
            << "x" << entry->result.rowCount << ", buckets:" << entry->result.bucketCount << ")" << std::endl;
    
    res.set_content(makeSuccess(data).dump(), "application/json; charset=utf-8");
});
server.Post("/api/density/bucket", [](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();
    
    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-bucket] FAILED (parse error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    const std::string queryId = body.value("queryId", "");
    if (queryId.empty()) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "queryId required").dump(), "application/json; charset=utf-8");
        return;
    }

    int bucketIndex = 0;
    if (!tryReadBucketIndex(body, bucketIndex)) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "bucketIndex invalid").dump(), "application/json; charset=utf-8");
        return;
    }

    auto entry = getDensityCacheById(queryId);
    if (!entry) {
        res.status = 404;
        res.set_content(makeError("NOT_FOUND", "density query cache expired or not found").dump(), "application/json; charset=utf-8");
        return;
    }

    if (bucketIndex < 0 || bucketIndex >= entry->result.bucketCount) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "bucketIndex out of range").dump(), "application/json; charset=utf-8");
        return;
    }

    const std::string payload = buildDensityBucketArrowStream(*entry, bucketIndex);
    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    Debug() << "[density-bucket] " << totalMs << "ms (bucket:" << bucketIndex 
            << ", cells:" << entry->bucketSummaries[static_cast<std::size_t>(bucketIndex)].nonZeroCount
            << ")" << std::endl;

    res.set_content(payload, kArrowStreamContentType);
});
server.Post("/api/density/cell-trend", [](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();
    
    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[density-cell-trend] FAILED (parse error) in " 
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << "ms" << std::endl;
        return;
    }

    const std::string queryId = body.value("queryId", "");
    if (queryId.empty()) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "queryId required").dump(), "application/json; charset=utf-8");
        return;
    }

    int gx = 0;
    int gy = 0;
    if (!tryReadGridIndex(body, "gx", gx) || !tryReadGridIndex(body, "gy", gy)) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "gx or gy invalid").dump(), "application/json; charset=utf-8");
        return;
    }

    auto entry = getDensityCacheById(queryId);
    if (!entry) {
        res.status = 404;
        res.set_content(makeError("NOT_FOUND", "density query cache expired or not found").dump(), "application/json; charset=utf-8");
        return;
    }

    if (gx < 0 || gx >= entry->result.columnCount || gy < 0 || gy >= entry->result.rowCount) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "gx or gy out of range").dump(), "application/json; charset=utf-8");
        return;
    }

    const std::string payload = buildDensityTrendArrowStream(*entry, gx, gy);
    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();
    Debug() << "[density-cell-trend] " << totalMs << "ms (gx:" << gx << ", gy:" << gy << ")" << std::endl;
    res.set_content(payload, kArrowStreamContentType);
});
server.Post("/api/region-flow/bidirectional", [](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();

    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        const auto t_end = std::chrono::steady_clock::now();
        Debug() << "[region-flow-bidirectional] FAILED (parse error) in "
                << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count()
                << "ms" << std::endl;
        return;
    }

    auto tryReadDouble = [&](const char* key, double& out) {
        if (!body.contains(key) || !body[key].is_number()) return false;
        out = body[key].get<double>();
        return true;
    };

    auto tryReadInt64 = [&](const char* key, long long& out) {
        if (!body.contains(key) || !body[key].is_number_integer()) return false;
        out = body[key].get<long long>();
        return true;
    };

    auto tryReadInt = [&](const char* key, int& out) {
        if (!body.contains(key) || !body[key].is_number_integer()) return false;
        out = body[key].get<int>();
        return true;
    };

    double minLonA = 0.0, minLatA = 0.0, maxLonA = 0.0, maxLatA = 0.0;
    double minLonB = 0.0, minLatB = 0.0, maxLonB = 0.0, maxLatB = 0.0;
    long long tStartValue = 0;
    long long bucketSize = 0;
    long long deltaT = 0;
    int bucketCount = 0;

    if (!tryReadDouble("minLonA", minLonA) ||
        !tryReadDouble("minLatA", minLatA) ||
        !tryReadDouble("maxLonA", maxLonA) ||
        !tryReadDouble("maxLatA", maxLatA) ||
        !tryReadDouble("minLonB", minLonB) ||
        !tryReadDouble("minLatB", minLatB) ||
        !tryReadDouble("maxLonB", maxLonB) ||
        !tryReadDouble("maxLatB", maxLatB) ||
        !tryReadInt64("tStart", tStartValue) ||
        !tryReadInt64("bucketSize", bucketSize) ||
        !tryReadInt("bucketCount", bucketCount) ||
        !tryReadInt64("deltaT", deltaT)) {
        res.status = 400;
        res.set_content(
            makeError("INVALID_ARGUMENT",
                      "required fields: minLonA,minLatA,maxLonA,maxLatA,minLonB,minLatB,maxLonB,maxLatB,tStart,bucketSize,bucketCount,deltaT").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (minLonA > maxLonA || minLatA > maxLatA ||
        minLonB > maxLonB || minLatB > maxLatB) {
        res.status = 400;
        res.set_content(
            makeError("INVALID_ARGUMENT", "invalid rectangle bounds").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (bucketSize <= 0 || bucketCount <= 0 || deltaT < 0) {
        res.status = 400;
        res.set_content(
            makeError("INVALID_ARGUMENT", "bucketSize>0, bucketCount>0, deltaT>=0 required").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (!DataManager::hasQuadTree()) {
        res.status = 503;
        res.set_content(
            makeError("SERVICE_UNAVAILABLE", "quadtree not ready").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    const auto buckets = DataManager::queryBidirectionalFlow(
        minLonA, minLatA, maxLonA, maxLatA,
        minLonB, minLatB, maxLonB, maxLatB,
        tStartValue, bucketSize, bucketCount, deltaT
    );

    json data;
    data["tStart"] = tStartValue;
    data["bucketSize"] = bucketSize;
    data["bucketCount"] = bucketCount;
    data["deltaT"] = deltaT;

    data["regionA"] = {
        {"minLon", minLonA},
        {"minLat", minLatA},
        {"maxLon", maxLonA},
        {"maxLat", maxLatA}
    };
    data["regionB"] = {
        {"minLon", minLonB},
        {"minLat", minLatB},
        {"maxLon", maxLonB},
        {"maxLat", maxLatB}
    };

    long long totalAtoB = 0;
    long long totalBtoA = 0;
    json arr = json::array();

    for (const auto& bucket : buckets) {
        arr.push_back({
            {"bucketStart", bucket.bucketStart},
            {"aToB", bucket.aToB},
            {"bToA", bucket.bToA}
        });
        totalAtoB += bucket.aToB;
        totalBtoA += bucket.bToA;
    }

    data["buckets"] = std::move(arr);
    data["summary"] = {
        {"totalAtoB", totalAtoB},
        {"totalBtoA", totalBtoA}
    };

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    data["elapsedMs"] = totalMs;

    json resp;
    resp["success"] = true;
    resp["data"] = std::move(data);

    Debug() << "[region-flow-bidirectional] " << totalMs << "ms"
            << " (bucketCount:" << bucketCount
            << ", bucketSize:" << bucketSize
            << ", deltaT:" << deltaT
            << ", totalAtoB:" << totalAtoB
            << ", totalBtoA:" << totalBtoA
            << ")" << std::endl;

    res.set_content(resp.dump(), "application/json; charset=utf-8");
});

//新增：单区域关联流量统计（F6功能）
server.Post("/api/region-flow/single", [this](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();

    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(
            makeError("INVALID_JSON", errorMessage).dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    auto tryReadDouble = [&](const char* key, double& out) {
        if (!body.contains(key) || !body[key].is_number()) return false;
        out = body[key].get<double>();
        return true;
    };

    auto tryReadInt64 = [&](const char* key, long long& out) {
        if (!body.contains(key) || !body[key].is_number_integer()) return false;
        out = body[key].get<long long>();
        return true;
    };

    auto tryReadInt = [&](const char* key, int& out) {
        if (!body.contains(key) || !body[key].is_number_integer()) return false;
        out = body[key].get<int>();
        return true;
    };

    double minLon = 0.0;
    double minLat = 0.0;
    double maxLon = 0.0;
    double maxLat = 0.0;
    long long tStartValue = 0;
    long long bucketSize = 0;
    long long deltaT = 0;
    int bucketCount = 0;

    if (!tryReadDouble("minLon", minLon) ||
        !tryReadDouble("minLat", minLat) ||
        !tryReadDouble("maxLon", maxLon) ||
        !tryReadDouble("maxLat", maxLat) ||
        !tryReadInt64("tStart", tStartValue) ||
        !tryReadInt64("bucketSize", bucketSize) ||
        !tryReadInt("bucketCount", bucketCount) ||
        !tryReadInt64("deltaT", deltaT)) {
        res.status = 400;
        res.set_content(
            makeError(
                "INVALID_ARGUMENT",
                "required fields: minLon,minLat,maxLon,maxLat,tStart,bucketSize,bucketCount,deltaT"
            ).dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (minLon > maxLon || minLat > maxLat) {
        res.status = 400;
        res.set_content(
            makeError("INVALID_ARGUMENT", "invalid target rectangle bounds").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (bucketSize <= 0 || bucketCount <= 0 || deltaT < 0) {
        res.status = 400;
        res.set_content(
            makeError("INVALID_ARGUMENT", "bucketSize>0, bucketCount>0, deltaT>=0 required").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (!DataManager::hasQuadTree()) {
        res.status = 503;
        res.set_content(
            makeError("SERVICE_UNAVAILABLE", "quadtree not ready").dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    const auto buckets = DataManager::querySingleRegionFlow(
        minLon, minLat,
        maxLon, maxLat,
        m_config.minLon, m_config.minLat,
        m_config.maxLon, m_config.maxLat,
        tStartValue,
        bucketSize,
        bucketCount,
        deltaT
    );

    json data;
    data["tStart"] = tStartValue;
    data["bucketSize"] = bucketSize;
    data["bucketCount"] = bucketCount;
    data["deltaT"] = deltaT;

    data["targetRegion"] = {
        {"minLon", minLon},
        {"minLat", minLat},
        {"maxLon", maxLon},
        {"maxLat", maxLat}
    };

    data["globalBounds"] = {
        {"minLon", m_config.minLon},
        {"minLat", m_config.minLat},
        {"maxLon", m_config.maxLon},
        {"maxLat", m_config.maxLat}
    };

    double totalIncoming = 0.0;
    double totalOutgoing = 0.0;
    json arr = json::array();

    for (const auto& bucket : buckets) {
        arr.push_back({
            {"bucketStart", bucket.bucketStart},
            {"incoming", bucket.incoming},
            {"outgoing", bucket.outgoing}
        });

        totalIncoming += bucket.incoming;
        totalOutgoing += bucket.outgoing;
    }

    data["buckets"] = std::move(arr);
    data["summary"] = {
        {"totalIncoming", totalIncoming},
        {"totalOutgoing", totalOutgoing},
        {"netFlow", totalIncoming - totalOutgoing}
    };

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    data["elapsedMs"] = totalMs;

    json resp;
    resp["success"] = true;
    resp["data"] = std::move(data);

    Debug() << "[region-flow-single] " << totalMs << "ms"
            << " (bucketCount:" << bucketCount
            << ", bucketSize:" << bucketSize
            << ", deltaT:" << deltaT
            << ", totalIncoming:" << totalIncoming
            << ", totalOutgoing:" << totalOutgoing
            << ")" << std::endl;

    res.set_content(resp.dump(), "application/json; charset=utf-8");
});

server.Post("/api/frequent-paths", [this](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();

    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        return;
    }

    int k = 10;
    double minLengthMeters = 0.0;
    std::string dbPath;

    if (body.contains("k") && body["k"].is_number_integer()) {
        k = body["k"].get<int>();
    }
    if (body.contains("minLengthMeters") && body["minLengthMeters"].is_number()) {
        minLengthMeters = body["minLengthMeters"].get<double>();
    }
    if (body.contains("dbPath") && body["dbPath"].is_string()) {
        dbPath = body["dbPath"].get<std::string>();
    }

    if (k <= 0 || k > 100) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "k must be between 1 and 100").dump(), "application/json; charset=utf-8");
        return;
    }
    if (minLengthMeters < 0.0) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "minLengthMeters must be >= 0").dump(), "application/json; charset=utf-8");
        return;
    }

    if (dbPath.empty()) {
        dbPath = (fs::path(m_config.dataDir) / "frequent_paths.db").lexically_normal().string();
    }

    FrequentPathQuery query;
    query.k = k;
    query.minLengthMeters = minLengthMeters;
    query.dbPath = dbPath;

    std::vector<FrequentPathRecord> records;
    try {
        records = FrequentPathManager::queryTopK(query);
    } catch (const std::exception& ex) {
        res.status = 500;
        res.set_content(makeError("FREQUENT_PATH_QUERY_FAILED", ex.what()).dump(), "application/json; charset=utf-8");
        return;
    }

    json paths = json::array();
    for (const auto& record : records) {
        json points = json::array();
        for (const auto& point : record.points) {
            points.push_back({
                {"lon", point.lon},
                {"lat", point.lat}
            });
        }

        paths.push_back({
            {"rank", record.rank},
            {"frequency", record.frequency},
            {"lengthMeters", record.lengthMeters},
            {"cellCount", record.cellCount},
            {"points", std::move(points)}
        });
    }

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    json data;
    data["k"] = k;
    data["minLengthMeters"] = minLengthMeters;
    data["dbPath"] = dbPath;
    data["paths"] = std::move(paths);
    data["elapsedMs"] = totalMs;

    json resp;
    resp["success"] = true;
    resp["data"] = std::move(data);

    Debug() << "[frequent-paths] " << totalMs << "ms"
            << " (k:" << k
            << ", minLengthMeters:" << minLengthMeters
            << ", resultCount:" << records.size()
            << ")" << std::endl;

    res.set_content(resp.dump(), "application/json; charset=utf-8");
});

server.Post("/api/frequent-paths/region-to-region", [this](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();

    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        return;
    }

    auto tryReadDouble = [&](const char* key, double& out) {
        if (!body.contains(key) || !body[key].is_number()) return false;
        out = body[key].get<double>();
        return true;
    };

    int k = 10;
    double minLengthMeters = 0.0;
    std::string dbPath;
    double minLonA = 0.0, minLatA = 0.0, maxLonA = 0.0, maxLatA = 0.0;
    double minLonB = 0.0, minLatB = 0.0, maxLonB = 0.0, maxLatB = 0.0;

    if (body.contains("k") && body["k"].is_number_integer()) {
        k = body["k"].get<int>();
    }
    if (body.contains("minLengthMeters") && body["minLengthMeters"].is_number()) {
        minLengthMeters = body["minLengthMeters"].get<double>();
    }
    if (body.contains("dbPath") && body["dbPath"].is_string()) {
        dbPath = body["dbPath"].get<std::string>();
    }

    if (!tryReadDouble("minLonA", minLonA) ||
        !tryReadDouble("minLatA", minLatA) ||
        !tryReadDouble("maxLonA", maxLonA) ||
        !tryReadDouble("maxLatA", maxLatA) ||
        !tryReadDouble("minLonB", minLonB) ||
        !tryReadDouble("minLatB", minLatB) ||
        !tryReadDouble("maxLonB", maxLonB) ||
        !tryReadDouble("maxLatB", maxLatB)) {
        res.status = 400;
        res.set_content(
            makeError(
                "INVALID_ARGUMENT",
                "required fields: minLonA,minLatA,maxLonA,maxLatA,minLonB,minLatB,maxLonB,maxLatB"
            ).dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (k <= 0 || k > 100) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "k must be between 1 and 100").dump(), "application/json; charset=utf-8");
        return;
    }
    if (minLengthMeters < 0.0) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "minLengthMeters must be >= 0").dump(), "application/json; charset=utf-8");
        return;
    }
    if (minLonA > maxLonA || minLatA > maxLatA || minLonB > maxLonB || minLatB > maxLatB) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "invalid rectangle bounds").dump(), "application/json; charset=utf-8");
        return;
    }

    if (dbPath.empty()) {
        dbPath = (fs::path(m_config.dataDir) / "frequent_paths.db").lexically_normal().string();
    }

    FrequentPathRegionQuery query;
    query.k = k;
    query.minLengthMeters = minLengthMeters;
    query.dbPath = dbPath;
    query.minLonA = minLonA;
    query.minLatA = minLatA;
    query.maxLonA = maxLonA;
    query.maxLatA = maxLatA;
    query.minLonB = minLonB;
    query.minLatB = minLatB;
    query.maxLonB = maxLonB;
    query.maxLatB = maxLatB;

    std::vector<FrequentPathRecord> records;
    try {
        records = FrequentPathManager::queryTopKBetweenRegions(query);
    } catch (const std::exception& ex) {
        res.status = 500;
        res.set_content(makeError("FREQUENT_PATH_REGION_QUERY_FAILED", ex.what()).dump(), "application/json; charset=utf-8");
        return;
    }

    json paths = json::array();
    for (const auto& record : records) {
        json points = json::array();
        for (const auto& point : record.points) {
            points.push_back({
                {"lon", point.lon},
                {"lat", point.lat}
            });
        }

        paths.push_back({
            {"rank", record.rank},
            {"frequency", record.frequency},
            {"lengthMeters", record.lengthMeters},
            {"cellCount", record.cellCount},
            {"points", std::move(points)}
        });
    }

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    json data;
    data["k"] = k;
    data["minLengthMeters"] = minLengthMeters;
    data["dbPath"] = dbPath;
    data["regionA"] = {
        {"minLon", minLonA},
        {"minLat", minLatA},
        {"maxLon", maxLonA},
        {"maxLat", maxLatA}
    };
    data["regionB"] = {
        {"minLon", minLonB},
        {"minLat", minLatB},
        {"maxLon", maxLonB},
        {"maxLat", maxLatB}
    };
    data["paths"] = std::move(paths);
    data["elapsedMs"] = totalMs;

    json resp;
    resp["success"] = true;
    resp["data"] = std::move(data);

    Debug() << "[frequent-paths-region] " << totalMs << "ms"
            << " (k:" << k
            << ", minLengthMeters:" << minLengthMeters
            << ", resultCount:" << records.size()
            << ")" << std::endl;

    res.set_content(resp.dump(), "application/json; charset=utf-8");
});

server.Post("/api/fastest-paths/region-to-region", [](const httplib::Request& req, httplib::Response& res) {
    const auto t_start = std::chrono::steady_clock::now();

    json body;
    std::string errorMessage;
    if (!parseJsonBody(req, body, errorMessage)) {
        res.status = 400;
        res.set_content(makeError("INVALID_JSON", errorMessage).dump(), "application/json; charset=utf-8");
        return;
    }

    auto tryReadDouble = [&](const char* key, double& out) {
        if (!body.contains(key) || !body[key].is_number()) return false;
        out = body[key].get<double>();
        return true;
    };
    auto tryReadInt64 = [&](const char* key, long long& out) {
        if (!body.contains(key) || !body[key].is_number_integer()) return false;
        out = body[key].get<long long>();
        return true;
    };
    auto tryReadInt = [&](const char* key, int& out) {
        if (!body.contains(key) || !body[key].is_number_integer()) return false;
        out = body[key].get<int>();
        return true;
    };

    double minLonA = 0.0, minLatA = 0.0, maxLonA = 0.0, maxLatA = 0.0;
    double minLonB = 0.0, minLatB = 0.0, maxLonB = 0.0, maxLatB = 0.0;
    long long tStartValue = 0;
    long long bucketSize = 0;
    long long deltaT = 0;
    int bucketCount = 0;

    if (!tryReadDouble("minLonA", minLonA) ||
        !tryReadDouble("minLatA", minLatA) ||
        !tryReadDouble("maxLonA", maxLonA) ||
        !tryReadDouble("maxLatA", maxLatA) ||
        !tryReadDouble("minLonB", minLonB) ||
        !tryReadDouble("minLatB", minLatB) ||
        !tryReadDouble("maxLonB", maxLonB) ||
        !tryReadDouble("maxLatB", maxLatB) ||
        !tryReadInt64("tStart", tStartValue) ||
        !tryReadInt64("bucketSize", bucketSize) ||
        !tryReadInt("bucketCount", bucketCount) ||
        !tryReadInt64("deltaT", deltaT)) {
        res.status = 400;
        res.set_content(
            makeError(
                "INVALID_ARGUMENT",
                "required fields: minLonA,minLatA,maxLonA,maxLatA,minLonB,minLatB,maxLonB,maxLatB,tStart,bucketSize,bucketCount,deltaT"
            ).dump(),
            "application/json; charset=utf-8"
        );
        return;
    }

    if (minLonA > maxLonA || minLatA > maxLatA || minLonB > maxLonB || minLatB > maxLatB) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "invalid rectangle bounds").dump(), "application/json; charset=utf-8");
        return;
    }
    if (bucketSize <= 0 || bucketCount <= 0 || deltaT <= 0) {
        res.status = 400;
        res.set_content(makeError("INVALID_ARGUMENT", "bucketSize>0, bucketCount>0, deltaT>0 required").dump(), "application/json; charset=utf-8");
        return;
    }
    if (!DataManager::hasQuadTree()) {
        res.status = 503;
        res.set_content(makeError("SERVICE_UNAVAILABLE", "quadtree not ready").dump(), "application/json; charset=utf-8");
        return;
    }

    const auto buckets = DataManager::queryFastestPathsBetweenRegions(
        minLonA, minLatA, maxLonA, maxLatA,
        minLonB, minLatB, maxLonB, maxLatB,
        tStartValue, bucketSize, bucketCount, deltaT
    );

    json arr = json::array();
    for (const auto& bucket : buckets) {
        json points = json::array();
        for (const auto& point : bucket.points) {
            points.push_back({
                {"id", point.id},
                {"time", point.timestamp},
                {"lon", point.lon},
                {"lat", point.lat}
            });
        }

        arr.push_back({
            {"bucketStart", bucket.bucketStart},
            {"found", bucket.found},
            {"taxiId", bucket.taxiId},
            {"leaveTime", bucket.leaveTime},
            {"enterTime", bucket.enterTime},
            {"travelTime", bucket.travelTime},
            {"points", std::move(points)}
        });
    }

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start
    ).count();

    json data;
    data["tStart"] = tStartValue;
    data["bucketSize"] = bucketSize;
    data["bucketCount"] = bucketCount;
    data["deltaT"] = deltaT;
    data["buckets"] = std::move(arr);
    data["elapsedMs"] = totalMs;

    json resp;
    resp["success"] = true;
    resp["data"] = std::move(data);

    Debug() << "[fastest-paths-region] " << totalMs << "ms"
            << " (bucketCount:" << bucketCount
            << ", bucketSize:" << bucketSize
            << ", deltaT:" << deltaT
            << ")" << std::endl;

    res.set_content(resp.dump(), "application/json; charset=utf-8");
});

    server.set_mount_point("/", webRoot.c_str());

    server.Get("/", [webRoot](const httplib::Request&, httplib::Response& res) {
        const fs::path indexPath = fs::path(webRoot) / "index.html";
        if (!fs::exists(indexPath)) {
            res.status = 404;
            res.set_content(makeError("NOT_FOUND", "index.html not found").dump(), "application/json; charset=utf-8");
            return;
        }

        const std::string content = readTextFile(indexPath);
        if (content.empty()) {
            res.status = 500;
            res.set_content(makeError("FILE_ERROR", "cannot open index.html").dump(), "application/json; charset=utf-8");
            return;
        }

        res.set_content(content, "text/html; charset=utf-8");
    });

    const std::string host = "0.0.0.0";
    return server.listen(host.c_str(), static_cast<int>(port));
}
