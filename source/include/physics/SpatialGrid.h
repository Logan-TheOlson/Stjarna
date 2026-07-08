#pragma once
#include <span>
#include <vector>

class SpatialGrid {
public:
    SpatialGrid() = default;
    SpatialGrid(float halfW, float halfH, float cellSize)
        : cellSize_(cellSize)
        , originX_(-halfW), originY_(-halfH)
        , cols_(static_cast<int>(2.0f * halfW / cellSize) + 1)
        , rows_(static_cast<int>(2.0f * halfH / cellSize) + 1)
        , cells_(cols_ * rows_)
    {}

    void Clear() { for (auto& c : cells_) c.clear(); }

    void Insert(int idx, float x, float y) {
        int col = cellCol(x), row = cellRow(y);
        if (col < 0 || col >= cols_ || row < 0 || row >= rows_) return;
        cells_[row * cols_ + col].push_back(idx);
    }

    std::span<const int> GetCell(int col, int row) const {
        if (col < 0 || col >= cols_ || row < 0 || row >= rows_) return {};
        return cells_[row * cols_ + col];
    }

    int Cols() const { return cols_; }
    int Rows() const { return rows_; }

private:
    int cellCol(float x) const { return static_cast<int>((x - originX_) / cellSize_); }
    int cellRow(float y) const { return static_cast<int>((y - originY_) / cellSize_); }

    float cellSize_{ 1.0f };
    float originX_{ 0.0f }, originY_{ 0.0f };
    int   cols_{ 0 }, rows_{ 0 };
    std::vector<std::vector<int>> cells_;
};
