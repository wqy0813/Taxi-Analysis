#ifndef QUADTREE_H
#define QUADTREE_H

#include <vector>

struct Rect {
    double x, y;
    double w, h;

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
};

class QuadNode {
public:
    Rect boundary;
    int capacity;
    std::vector<int> points;
    int depth;

    QuadNode* nw;
    QuadNode* ne;
    QuadNode* sw;
    QuadNode* se;
    bool divided;

    QuadNode(Rect b, int cap = 500, int nodeDepth = 0);
    ~QuadNode();

    bool insert(int pointIdx, const std::vector<struct GPSPoint>& allData);
    void subdivide(const std::vector<struct GPSPoint>& allData);
    void query(const Rect& range, std::vector<int>& found, const std::vector<struct GPSPoint>& allData);

private:
    bool canSubdivide() const;
};

#endif
