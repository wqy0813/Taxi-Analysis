#include "quadtree.h"
#include "datamanager.h"

#include <algorithm>
#include <utility>

QuadNode::QuadNode(Rect b,
                   int cap,
                   int nodeDepth,
                   int maxNodeDepth,
                   double minNodeCellSize)
    : boundary(b),
    capacity(cap),
    depth(nodeDepth),
    maxDepth(maxNodeDepth),
    minCellSize(minNodeCellSize),
    hasLevelIndex(false),
    nw(nullptr),
    ne(nullptr),
    sw(nullptr),
    se(nullptr),
    divided(false) {}

QuadNode::~QuadNode() {
    if (divided) {
        delete nw;
        delete ne;
        delete sw;
        delete se;
    }
}

bool QuadNode::isLeaf() const {
    return !divided;
}

bool QuadNode::canSubdivide() const {
    if (depth < maxDepth &&
        boundary.w > minCellSize &&
        boundary.h > minCellSize) {
        return true;
    }

    DataManager::exceptionalNodes.insert(this);
    return false;
}

void QuadNode::insertSortedByTime(int pointIdx,
                                  const std::vector<GPSPoint>& allData) {
    const long long ts = allData[pointIdx].timestamp;

    if (points.empty() || allData[points.back()].timestamp <= ts) {
        points.push_back(pointIdx);
        return;
    }

    auto it = std::lower_bound(
        points.begin(), points.end(), ts,
        [&](int idx, long long value) {
            return allData[idx].timestamp < value;
        }
        );

    points.insert(it, pointIdx);
}

bool QuadNode::insert(int pointIdx, const std::vector<GPSPoint>& allData) {
    const GPSPoint& p = allData[pointIdx];

    if (!boundary.contains(p.lon, p.lat)) {
        return false;
    }

    if (!divided) {
        if (static_cast<int>(points.size()) < capacity || !canSubdivide()) {
            insertSortedByTime(pointIdx, allData);
            return true;
        }

        subdivide(allData);
    }

    if (nw->insert(pointIdx, allData)) return true;
    if (ne->insert(pointIdx, allData)) return true;
    if (sw->insert(pointIdx, allData)) return true;
    if (se->insert(pointIdx, allData)) return true;

    // 极少数边界情况兜底
    insertSortedByTime(pointIdx, allData);
    return true;
}

void QuadNode::subdivide(const std::vector<GPSPoint>& allData) {
    const double x = boundary.x;
    const double y = boundary.y;
    const double w = boundary.w / 2.0;
    const double h = boundary.h / 2.0;

    nw = new QuadNode({x - w, y + h, w, h}, capacity, depth + 1, maxDepth, minCellSize);
    ne = new QuadNode({x + w, y + h, w, h}, capacity, depth + 1, maxDepth, minCellSize);
    sw = new QuadNode({x - w, y - h, w, h}, capacity, depth + 1, maxDepth, minCellSize);
    se = new QuadNode({x + w, y - h, w, h}, capacity, depth + 1, maxDepth, minCellSize);
    divided = true;

    std::vector<int> oldPoints = std::move(points);
    points.clear();

    for (int idx : oldPoints) {
        if (nw->insert(idx, allData)) continue;
        if (ne->insert(idx, allData)) continue;
        if (sw->insert(idx, allData)) continue;
        if (se->insert(idx, allData)) continue;

        insertSortedByTime(idx, allData);
    }
}

void QuadNode::querySpatial(const Rect& range,
                            std::vector<int>& found,
                            const std::vector<GPSPoint>& allData) const {
    if (!boundary.intersects(range)) {
        return;
    }

    if (isLeaf()) {
        if (boundary.fullyInside(range)) {
            found.insert(found.end(), points.begin(), points.end());
            return;
        }

        for (int idx : points) {
            const GPSPoint& p = allData[idx];
            if (range.contains(p.lon, p.lat)) {
                found.push_back(idx);
            }
        }
        return;
    }

    if (nw) nw->querySpatial(range, found, allData);
    if (ne) ne->querySpatial(range, found, allData);
    if (sw) sw->querySpatial(range, found, allData);
    if (se) se->querySpatial(range, found, allData);
}

void QuadNode::appendTimeRange(long long startTime,
                               long long endTime,
                               std::vector<int>& found,
                               const std::vector<GPSPoint>& allData) const {
    auto left = std::lower_bound(
        points.begin(), points.end(), startTime,
        [&](int idx, long long value) {
            return allData[idx].timestamp < value;
        }
        );

    auto right = std::upper_bound(
        points.begin(), points.end(), endTime,
        [&](long long value, int idx) {
            return value < allData[idx].timestamp;
        }
        );

    found.insert(found.end(), left, right);
}

void QuadNode::appendTimeRangeFromIndex(const std::vector<int>& sortedIndex,
                                        long long startTime,
                                        long long endTime,
                                        std::vector<int>& found,
                                        const std::vector<GPSPoint>& allData) const {
    auto left = std::lower_bound(
        sortedIndex.begin(), sortedIndex.end(), startTime,
        [&](int idx, long long value) {
            return allData[idx].timestamp < value;
        }
        );

    auto right = std::upper_bound(
        sortedIndex.begin(), sortedIndex.end(), endTime,
        [&](long long value, int idx) {
            return value < allData[idx].timestamp;
        }
        );

    found.insert(found.end(), left, right);
}

std::vector<int> QuadNode::mergeTwoSortedIndexVectors(
    const std::vector<int>& a,
    const std::vector<int>& b,
    const std::vector<GPSPoint>& allData) {

    std::vector<int> result;
    result.reserve(a.size() + b.size());

    size_t i = 0;
    size_t j = 0;

    while (i < a.size() && j < b.size()) {
        if (allData[a[i]].timestamp <= allData[b[j]].timestamp) {
            result.push_back(a[i]);
            ++i;
        } else {
            result.push_back(b[j]);
            ++j;
        }
    }

    while (i < a.size()) {
        result.push_back(a[i]);
        ++i;
    }

    while (j < b.size()) {
        result.push_back(b[j]);
        ++j;
    }

    return result;
}

std::vector<int> QuadNode::buildSortedIndexBottomUp(
    const std::unordered_set<int>& targetDepths,
    const std::vector<GPSPoint>& allData) {

    hasLevelIndex = false;
    levelSortedPoints.clear();

    if (isLeaf()) {
        // 叶子节点 points 本来就已经按 timestamp 升序
        if (targetDepths.find(depth) != targetDepths.end()) {
            hasLevelIndex = true;
            levelSortedPoints = points;
        }
        return points;
    }

    std::vector<int> merged;

    if (nw) {
        merged = nw->buildSortedIndexBottomUp(targetDepths, allData);
    }

    if (ne) {
        std::vector<int> child = ne->buildSortedIndexBottomUp(targetDepths, allData);
        merged = mergeTwoSortedIndexVectors(merged, child, allData);
    }

    if (sw) {
        std::vector<int> child = sw->buildSortedIndexBottomUp(targetDepths, allData);
        merged = mergeTwoSortedIndexVectors(merged, child, allData);
    }

    if (se) {
        std::vector<int> child = se->buildSortedIndexBottomUp(targetDepths, allData);
        merged = mergeTwoSortedIndexVectors(merged, child, allData);
    }

    // 如果当前层是目标层，保存这一层节点整棵子树的有序索引
    if (targetDepths.find(depth) != targetDepths.end()) {
        hasLevelIndex = true;
        levelSortedPoints = merged;
    }

    return merged;
}

void QuadNode::buildSortedIndexForDepths(const std::unordered_set<int>& targetDepths,
                                         const std::vector<GPSPoint>& allData) {
    buildSortedIndexBottomUp(targetDepths, allData);
}

void QuadNode::querySpatioTemporal(const Rect& range,
                                   long long startTime,
                                   long long endTime,
                                   std::vector<int>& found,
                                   const std::vector<GPSPoint>& allData) const {
    if (!boundary.intersects(range)) {
        return;
    }

    // 当前节点如果属于“建立了层级时序索引的层”，并且空间被完全覆盖，
    // 直接在该层索引上二分，不再往下递归
    if (hasLevelIndex && boundary.fullyInside(range) && !levelSortedPoints.empty()) {
        appendTimeRangeFromIndex(levelSortedPoints, startTime, endTime, found, allData);
        return;
    }

    if (isLeaf()) {
        if (boundary.fullyInside(range)) {
            appendTimeRange(startTime, endTime, found, allData);
            return;
        }

        for (int idx : points) {
            const GPSPoint& p = allData[idx];
            if (range.contains(p.lon, p.lat) &&
                p.timestamp >= startTime &&
                p.timestamp <= endTime) {
                found.push_back(idx);
            }
        }
        return;
    }

    if (nw) nw->querySpatioTemporal(range, startTime, endTime, found, allData);
    if (ne) ne->querySpatioTemporal(range, startTime, endTime, found, allData);
    if (sw) sw->querySpatioTemporal(range, startTime, endTime, found, allData);
    if (se) se->querySpatioTemporal(range, startTime, endTime, found, allData);
}
void QuadNode::querySpatioTemporalUniqueIds(const Rect& range,
                                            long long startTime,
                                            long long endTime,
                                            std::unordered_set<int>& idSet,
                                            const std::vector<GPSPoint>& allData) const {
    if (!boundary.intersects(range)) {
        return;
    }

    if (hasLevelIndex && boundary.fullyInside(range) && !levelSortedPoints.empty()) {
        auto left = std::lower_bound(
            levelSortedPoints.begin(), levelSortedPoints.end(), startTime,
            [&](int idx, long long value) {
                return allData[idx].timestamp < value;
            }
            );

        auto right = std::upper_bound(
            levelSortedPoints.begin(), levelSortedPoints.end(), endTime,
            [&](long long value, int idx) {
                return value < allData[idx].timestamp;
            }
            );

        for (auto it = left; it != right; ++it) {
            idSet.insert(allData[*it].id);
        }
        return;
    }

    if (isLeaf()) {
        if (boundary.fullyInside(range)) {
            auto left = std::lower_bound(
                points.begin(), points.end(), startTime,
                [&](int idx, long long value) {
                    return allData[idx].timestamp < value;
                }
                );

            auto right = std::upper_bound(
                points.begin(), points.end(), endTime,
                [&](long long value, int idx) {
                    return value < allData[idx].timestamp;
                }
                );

            for (auto it = left; it != right; ++it) {
                idSet.insert(allData[*it].id);
            }
            return;
        }

        for (int idx : points) {
            const GPSPoint& p = allData[idx];
            if (range.contains(p.lon, p.lat) &&
                p.timestamp >= startTime &&
                p.timestamp <= endTime) {
                idSet.insert(p.id);
            }
        }
        return;
    }

    if (nw) nw->querySpatioTemporalUniqueIds(range, startTime, endTime, idSet, allData);
    if (ne) ne->querySpatioTemporalUniqueIds(range, startTime, endTime, idSet, allData);
    if (sw) sw->querySpatioTemporalUniqueIds(range, startTime, endTime, idSet, allData);
    if (se) se->querySpatioTemporalUniqueIds(range, startTime, endTime, idSet, allData);
}