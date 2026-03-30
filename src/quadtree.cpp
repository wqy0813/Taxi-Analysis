#include "quadtree.h"
#include "datamanager.h"

#include <utility>

namespace {
constexpr int kMaxQuadTreeDepth = 24;
constexpr double kMinQuadCellSize = 1e-7;
}

QuadNode::QuadNode(Rect b, int cap, int nodeDepth)
    : boundary(b),
      capacity(cap),
      depth(nodeDepth),
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

bool QuadNode::canSubdivide() const {
    return depth < kMaxQuadTreeDepth &&
           boundary.w > kMinQuadCellSize &&
           boundary.h > kMinQuadCellSize;
}

void QuadNode::subdivide(const std::vector<GPSPoint>& allData) {
    const double x = boundary.x;
    const double y = boundary.y;
    const double w = boundary.w / 2.0;
    const double h = boundary.h / 2.0;

    nw = new QuadNode({x - w, y + h, w, h}, capacity, depth + 1);
    ne = new QuadNode({x + w, y + h, w, h}, capacity, depth + 1);
    sw = new QuadNode({x - w, y - h, w, h}, capacity, depth + 1);
    se = new QuadNode({x + w, y - h, w, h}, capacity, depth + 1);
    divided = true;

    std::vector<int> existingPoints = std::move(points);
    points.clear();

    for (int idx : existingPoints) {
        if (nw->insert(idx, allData) || ne->insert(idx, allData) ||
            sw->insert(idx, allData) || se->insert(idx, allData)) {
            continue;
        }

        points.push_back(idx);
    }
}

bool QuadNode::insert(int pointIdx, const std::vector<GPSPoint>& allData) {
    const GPSPoint& p = allData[pointIdx];
    if (!boundary.contains(p.lon, p.lat)) {
        return false;
    }

    if (!divided) {
        if (points.size() < static_cast<size_t>(capacity) || !canSubdivide()) {
            points.push_back(pointIdx);
            return true;
        }

        subdivide(allData);
    }

    if (nw->insert(pointIdx, allData) || ne->insert(pointIdx, allData) ||
        sw->insert(pointIdx, allData) || se->insert(pointIdx, allData)) {
        return true;
    }

    points.push_back(pointIdx);
    return true;
}

void QuadNode::query(const Rect& range, std::vector<int>& found,
                     const std::vector<GPSPoint>& allData) {
    if (!boundary.intersects(range)) {
        return;
    }

    for (int idx : points) {
        const GPSPoint& p = allData[idx];
        if (range.contains(p.lon, p.lat)) {
            found.push_back(idx);
        }
    }

    if (!divided) {
        return;
    }

    if (nw) {
        nw->query(range, found, allData);
    }
    if (ne) {
        ne->query(range, found, allData);
    }
    if (sw) {
        sw->query(range, found, allData);
    }
    if (se) {
        se->query(range, found, allData);
    }
}
