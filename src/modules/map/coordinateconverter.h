#pragma once

#include "modules/map/maptypes.h"

/** 地图坐标规范化工具。输入和输出单位均为度。 */
class CoordinateConverter final
{
public:
    /**
     * 将 WGS-84 坐标转换为 GCJ-02。中国大陆范围外不施加偏移；无效输入原样返回，
     * 调用方仍需通过 GeoPoint::isValid() 拒绝无效结果。
     */
    static GeoPoint wgs84ToGcj02(const GeoPoint &point);

    /** 返回坐标是否位于 GCJ-02 偏移算法适用的中国大陆近似边界内。 */
    static bool usesGcj02Offset(const GeoPoint &point);
};
