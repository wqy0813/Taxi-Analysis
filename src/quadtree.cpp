#include "quadtree.h"
#include "datamanager.h" // 假设 GPSPoint 定义在这里

QuadNode::QuadNode(Rect b, int cap)
    : boundary(b), capacity(cap), nw(nullptr), ne(nullptr), sw(nullptr), se(nullptr), divided(false) {}

QuadNode::~QuadNode() {
    if (divided) {
        delete nw; delete ne; delete sw; delete se;
    }
}

// 分裂：把一个大盒子切成四个小盒子
void QuadNode::subdivide(const std::vector<GPSPoint>& allData) {
    double x = boundary.x;
    double y = boundary.y;
    double w = boundary.w / 2;
    double h = boundary.h / 2;

    nw = new QuadNode({x - w, y + h, w, h}, capacity);
    ne = new QuadNode({x + w, y + h, w, h}, capacity);
    sw = new QuadNode({x - w, y - h, w, h}, capacity);
    se = new QuadNode({x + w, y - h, w, h}, capacity);

    divided = true;

    // 关键：把当前节点原有的点重新分配给子节点
    for (int idx : points) {
        // 递归调用 insert，把点分发到新创建的四个儿子里
        this->insert(idx, allData);
    }
    points.clear(); // 当前节点不再存点，只做“路标”
}

// 插入：递归寻找属于自己的位置
bool QuadNode::insert(int pointIdx, const std::vector<GPSPoint>& allData) {
    const GPSPoint& p = allData[pointIdx];

    // 1. 如果点不在我的地盘，直接拒绝
    if (!boundary.contains(p.lon, p.lat)) return false;

    // 2. 如果还没分裂，且还没满，就存下这个点的下标
    if (!divided && points.size() < (size_t)capacity) {
        points.push_back(pointIdx);
        return true;
    }

    // 3. 如果满了且还没分裂，就执行分裂
    if (!divided) {
        subdivide(allData); // 修正：传入数据引用
    }

    // 4. 尝试丢给四个儿子
    if (nw->insert(pointIdx, allData)) return true;
    if (ne->insert(pointIdx, allData)) return true;
    if (sw->insert(pointIdx, allData)) return true;
    if (se->insert(pointIdx, allData)) return true;

    return false;
}
void QuadNode::query(const Rect& range, std::vector<int>& found,
                     const std::vector<GPSPoint>& allData) {
    if (!boundary.intersects(range)) {
        return;
    }

    if (!divided) {
        for (int idx : points) {
            const GPSPoint& p = allData[idx];
            if (range.contains(p.lon, p.lat)) {
                found.push_back(idx);
            }
        }
        return;
    }

    if (nw) nw->query(range, found, allData);
    if (ne) ne->query(range, found, allData);
    if (sw) sw->query(range, found, allData);
    if (se) se->query(range, found, allData);
}