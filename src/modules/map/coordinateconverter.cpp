#include "modules/map/coordinateconverter.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSemiMajorAxis = 6378245.0;
constexpr double kEccentricitySquared = 0.00669342162296594323;

double latitudeOffset(double longitude, double latitude)
{
    double value = -100.0 + 2.0 * longitude + 3.0 * latitude
                   + 0.2 * latitude * latitude
                   + 0.1 * longitude * latitude
                   + 0.2 * std::sqrt(std::abs(longitude));
    value += (20.0 * std::sin(6.0 * longitude * kPi)
              + 20.0 * std::sin(2.0 * longitude * kPi))
             * 2.0 / 3.0;
    value += (20.0 * std::sin(latitude * kPi)
              + 40.0 * std::sin(latitude / 3.0 * kPi))
             * 2.0 / 3.0;
    value += (160.0 * std::sin(latitude / 12.0 * kPi)
              + 320.0 * std::sin(latitude * kPi / 30.0))
             * 2.0 / 3.0;
    return value;
}

double longitudeOffset(double longitude, double latitude)
{
    double value = 300.0 + longitude + 2.0 * latitude
                   + 0.1 * longitude * longitude
                   + 0.1 * longitude * latitude
                   + 0.1 * std::sqrt(std::abs(longitude));
    value += (20.0 * std::sin(6.0 * longitude * kPi)
              + 20.0 * std::sin(2.0 * longitude * kPi))
             * 2.0 / 3.0;
    value += (20.0 * std::sin(longitude * kPi)
              + 40.0 * std::sin(longitude / 3.0 * kPi))
             * 2.0 / 3.0;
    value += (150.0 * std::sin(longitude / 12.0 * kPi)
              + 300.0 * std::sin(longitude / 30.0 * kPi))
             * 2.0 / 3.0;
    return value;
}

} // namespace

GeoPoint CoordinateConverter::wgs84ToGcj02(const GeoPoint &point)
{
    if (!point.isValid() || !usesGcj02Offset(point)) {
        return point;
    }

    double latitudeDelta = latitudeOffset(point.longitude - 105.0,
                                           point.latitude - 35.0);
    double longitudeDelta = longitudeOffset(point.longitude - 105.0,
                                             point.latitude - 35.0);
    const double latitudeRadians = point.latitude / 180.0 * kPi;
    const double sine = std::sin(latitudeRadians);
    const double magic = 1.0 - kEccentricitySquared * sine * sine;
    const double squareRootMagic = std::sqrt(magic);
    latitudeDelta = latitudeDelta * 180.0
                    / ((kSemiMajorAxis * (1.0 - kEccentricitySquared))
                       / (magic * squareRootMagic) * kPi);
    longitudeDelta = longitudeDelta * 180.0
                     / (kSemiMajorAxis / squareRootMagic
                        * std::cos(latitudeRadians) * kPi);
    return {point.latitude + latitudeDelta, point.longitude + longitudeDelta};
}

bool CoordinateConverter::usesGcj02Offset(const GeoPoint &point)
{
    // GCJ-02 常用公开近似边界；边界外必须保持 WGS-84 原坐标，避免全球坐标误偏移。
    return point.isValid()
           && point.longitude >= 72.004 && point.longitude <= 137.8347
           && point.latitude >= 0.8293 && point.latitude <= 55.8271;
}
