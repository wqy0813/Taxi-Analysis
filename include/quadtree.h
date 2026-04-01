#ifndef QUADTREE_H
#define QUADTREE_H

#include <vector>
#include <unordered_set>

struct GPSPoint;

struct Rect {
    double x, y;   // 中心
    double w, h;   // 半宽、半高

    bool contains(double px, double py) const {
        return (px >= x - w && px <= x + w &&
                py >= y - h && py <= y + h);
    }

    bool intersects(const Rect& range) const {
        return !(range.x - range.w > x + w ||
                 range.x + range.w < x - w ||
                 range.y - range.h > y + h ||
                 range.y + range.h < y - h);
    }

    bool fullyInside(const Rect& range) const {
        return (x - w >= range.x - range.w &&
                x + w <= range.x + range.w &&
                y - h >= range.y - range.h &&
                y + h <= range.y + range.h);
    }
};

class QuadNode {
public:
    Rect boundary;
    int capacity;
    int depth;
    int maxDepth;
    double minCellSize;

    // 叶子节点：按 timestamp 升序保存点索引
    std::vector<int> points;

    // 当前节点是否属于“被选中的索引层”
    bool hasLevelIndex;

    // 如果当前深度在目标层集合里，则保存整棵子树的按时间升序索引
    std::vector<int> levelSortedPoints;

    QuadNode* nw;
    QuadNode* ne;
    QuadNode* sw;
    QuadNode* se;
    bool divided;

    QuadNode(Rect b,
             int cap = 500,
             int nodeDepth = 0,
             int maxNodeDepth = 64,
             double minNodeCellSize = 1e-7);

    ~QuadNode();

    bool insert(int pointIdx, const std::vector<GPSPoint>& allData);
    void subdivide(const std::vector<GPSPoint>& allData);

    void querySpatial(const Rect& range,
                      std::vector<int>& found,
                      const std::vector<GPSPoint>& allData) const;

    void querySpatioTemporal(const Rect& range,
                             long long startTime,
                             long long endTime,
                             std::vector<int>& found,
                             const std::vector<GPSPoint>& allData) const;

    // 建树完成后调用：给多个指定层构建时序索引
    void buildSortedIndexForDepths(const std::unordered_set<int>& targetDepths,
                                   const std::vector<GPSPoint>& allData);
    void querySpatioTemporalUniqueIds(const Rect& range,
                                            long long startTime,
                                            long long endTime,
                                            std::unordered_set<int>& idSet,
                                      const std::vector<GPSPoint>& allData) const;

private:
    bool canSubdivide() const;
    bool isLeaf() const;

    void insertSortedByTime(int pointIdx,
                            const std::vector<GPSPoint>& allData);

    void appendTimeRange(long long startTime,
                         long long endTime,
                         std::vector<int>& found,
                         const std::vector<GPSPoint>& allData) const;

    void appendTimeRangeFromIndex(const std::vector<int>& sortedIndex,
                                  long long startTime,
                                  long long endTime,
                                  std::vector<int>& found,
                                  const std::vector<GPSPoint>& allData) const;

    // bottom-up 核心：返回“当前节点整棵子树”的按时间升序索引
    std::vector<int> buildSortedIndexBottomUp(
        const std::unordered_set<int>& targetDepths,
        const std::vector<GPSPoint>& allData);

    // 合并两个已经按 timestamp 升序的索引数组
    static std::vector<int> mergeTwoSortedIndexVectors(
        const std::vector<int>& a,
        const std::vector<int>& b,
        const std::vector<GPSPoint>& allData);
};

#endif