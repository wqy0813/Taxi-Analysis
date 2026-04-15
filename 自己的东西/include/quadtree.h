#ifndef QUADTREE_H
#define QUADTREE_H

#include <vector>

// 1. 定义一个矩形区域，用来表示地图范围
struct Rect {
    double x, y; // 中心点坐标
    double w, h; // 半宽高（从中心到边界的距离）

    // 检查一个 GPS 点是否在这个矩形内
    bool contains(double px, double py) const {
        return (px >= x - w && px <= x + w &&
                py >= y - h && py <= y + h);
    }

    // 检查另一个矩形是否与自己相交（用于查询）
    bool intersects(const Rect& range) const {
        return !(range.x - range.w > x + w ||
                 range.x + range.w < x - w ||
                 range.y - range.h > y + h ||
                 range.y + range.h < y - h);
    }
};

// 2. 四叉树节点
class QuadNode {
public:
    Rect boundary;          // 当前节点管辖的区域
    int capacity;           // 每个节点最多存多少个点，超过就分裂
    std::vector<int> points; // 存储点在主数组中的索引（节约内存）

    // 四个子节点指针
    QuadNode *nw, *ne, *sw, *se;
    bool divided;           // 标记是否已经分裂

    QuadNode(Rect b, int cap = 500);
    ~QuadNode();

    // 核心动作
    bool insert(int pointIdx, const std::vector<struct GPSPoint>& allData);
    void subdivide(const std::vector<struct GPSPoint>& allData);       // 执行分裂动作
    void query(const Rect& range, std::vector<int>& found, const std::vector<struct GPSPoint>& allData);
};

#endif
