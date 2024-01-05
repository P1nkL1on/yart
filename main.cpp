#include <cmath>
#include <algorithm>
#include <iostream>
#include <memory>
#include <numeric>

#include <QImage>
#include <QColorSpace>
#include <QVector>
#include <QVector3D>


namespace {

using Color = QVector3D;
class Light
{
public:
    virtual float power(
            const QVector3D &origin,
            const QVector3D &normalDirection) const = 0;
    Color color = Color(1, 1, 1);
    QVector3D center;
};

class Bulb : public Light
{
public:
    Bulb(const QVector3D &center, const Color &color)
    {
        this->center = center;
        this->color = color;
    }
    float power(
                const QVector3D &origin,
                const QVector3D &normalDirection) const override
    {
        const QVector3D v1 = (origin - center).normalized();
        const QVector3D v2 = normalDirection;
        const float rad = std::acos(QVector3D::dotProduct(v1, v2));
        if (std::abs(rad) > M_PI_2)
            return 0;
        return 1 - std::abs(rad) / M_PI_2;
    }
};

class Shape
{
public:
    virtual ~Shape() = default;
    virtual bool intersects(
            const QVector3D &origin,
            const QVector3D &direction,
            QVector3D *intersectionOrigin,
            QVector3D *normalDirection,
            QVector3D *reflectionDirection) const = 0;
    Color color = Color(1, 0, 0);
    float mirror = 0.0f;
};

class Sphere : public Shape
{
public:
    Sphere(const QVector3D &center, const float &radius, const Color &color, const float mirror)
        : center_(center), radius_(radius) { this->color = color; this->mirror = mirror; }
private:
    QVector3D center_;
    float radius_ = 0.0f;
    bool intersects(
            const QVector3D &origin,
            const QVector3D &direction,
            QVector3D *intersectionOrigin,
            QVector3D *normalDirection,
            QVector3D *reflectionDirection) const override
    {
        const QVector3D m = origin - center_;
        const float b = QVector3D::dotProduct(direction, m);
        const float c = QVector3D::dotProduct(m, m) - radius_ * radius_;
        if (c > 0.0f && b > 0.0f)
            return false;

        const float discr = b * b - c;
        if (discr < 0.0f)
            return false;

        // distance?    // aka starts inside the sphere
        const float t = std::max(0.0f, -b - std::sqrt(discr));
        const QVector3D intersection = origin + direction * t;
        const QVector3D normal = (intersection - center_).normalized();
        const QVector3D reflection = direction - 2 * normal * QVector3D::dotProduct(direction, normal);
        if (intersectionOrigin)
            *intersectionOrigin = intersection;
        if (normalDirection)
            *normalDirection = normal;
        if (reflectionDirection)
            *reflectionDirection = reflection;
        return true;
    }
};

template <typename T>
QVector<T> allBut(const QVector<T> &vector, const int index)
{
    auto res = vector;
    res.removeAt(index);
    return res;
}

Color cast(
        const QVector<std::shared_ptr<Shape>> &shapes,
        const QVector<std::shared_ptr<Light>> &lights,
        const QVector3D &origin,
        const QVector3D &direction,
        const Color &colorOnMiss,
        const Color &colorOnFullShade)
{
    const int nShapes = shapes.size();
    float shortestDistanceSquared = std::numeric_limits<float>::max();
    int shapeIndex = -1;
    QVector3D intersectionOrigin;
    QVector3D normalDirection;
    QVector3D reflectionDirection;
    for (int index = 0; index < nShapes; ++index) {
        const auto &shape = shapes.at(index);
        QVector3D intersection;
        QVector3D normal;
        QVector3D reflection;
        if (!shape->intersects(
                    origin,
                    direction,
                    &intersection,
                    &normal,
                    &reflection))
            continue;
        const float distanceSquared = (intersection - origin).lengthSquared();
        if (distanceSquared >= shortestDistanceSquared)
            continue;
        shortestDistanceSquared = distanceSquared;
        intersectionOrigin = intersection;
        normalDirection = normal;
        reflectionDirection = reflection;
        shapeIndex = index;
    }
    if (shapeIndex < 0)
        return colorOnMiss;

    const auto &shape = shapes.at(shapeIndex);
    const auto otherShapes = allBut(shapes, shapeIndex);
    Color colorSelf = shape->color;
    if (shape->mirror > 0.0f) {
        const Color colorMirrored = cast(
                    otherShapes,
                    lights,
                    intersectionOrigin,
                    reflectionDirection,
                    colorOnMiss,
                    colorOnFullShade);
        colorSelf = colorSelf * (1 - shape->mirror) + colorMirrored * shape->mirror;
    }
    Color colorMask = colorOnFullShade;
    for (const auto &light : lights) {
        bool isBlocked = false;
        for (const auto &otherShape : otherShapes) {
            if (!otherShape->intersects(
                        intersectionOrigin,
                        (intersectionOrigin - light->center).normalized(),
                        nullptr,
                        nullptr,
                        nullptr))
                continue;
            isBlocked = true;
            break;
        }
        if (isBlocked)
            continue;
        const float power = light->power(
                    intersectionOrigin,
                    normalDirection);
        if (power <= 0.0f)
            continue;
        colorMask += power * light->color;
    }
    return colorSelf * colorMask;
}

inline int toRgbComponent(const float value)
{
    return std::clamp(static_cast<int>(value * 255), 0, 255);
}

QRgb toRgb(const Color &color)
{
    return qRgb(toRgbComponent(color.x()), toRgbComponent(color.y()), toRgbComponent(color.z()));
}

}

int main(int, char **)
{
    using std::cout;

    const QVector<std::shared_ptr<Shape>> shapes = {
        std::shared_ptr<Shape>(new Sphere(QVector3D(0, 0, 0), 5,  Color(1.0, 0.5, 0.5), 0.9)),
        std::shared_ptr<Shape>(new Sphere(QVector3D(0, -12, 0), 4, Color(0.5, 1.0, 0.5), 0.9)),
        std::shared_ptr<Shape>(new Sphere(QVector3D(5, 8, 7), 3, Color(1, 1, 1), 0.5)),
        std::shared_ptr<Shape>(new Sphere(QVector3D(7, 5, 5), 2, Color(0.5, 0.5, 1.0), 0)),
        std::shared_ptr<Shape>(new Sphere(QVector3D(12, 4, 5), 1, Color(0.5, 0.5, 0.2), 0)),
        std::shared_ptr<Shape>(new Sphere(QVector3D(-100, 0, -50), 100, Color(0.5, 0.5, 0.5), 0.4)),
        std::shared_ptr<Shape>(new Sphere(QVector3D(-100, 0, 50), 100, Color(1, 1, 1), 0.4)),
    };
    const QVector<std::shared_ptr<Light>> lights = {
        std::shared_ptr<Light>(new Bulb(QVector3D(-20, -10, 20), Color(1, 1, 1) * 0.7)),
        std::shared_ptr<Light>(new Bulb(QVector3D(-20, -12, 22), Color(1, 1, 1) * 0.7)),
    };

    const float cameraSize = 30.0f;
    const QVector3D cameraOrigin(100, 0, 0);
    const QVector3D cameraDirection(-1, 0, 0);
    const int resolutionPrefered = 512;
    const int msaaMultiplier = 2;
    const Color colorOnMiss(0, 0, 1);
    const Color colorOnFullShade(0.1, 0.1, 0.1);

    QVector<int> resolutionsDownscaled;
    for (int resolutionDownscaled = resolutionPrefered * msaaMultiplier; resolutionDownscaled > 16; resolutionDownscaled /= 2)
        resolutionsDownscaled.push_front(resolutionDownscaled);

    for (int resolution : resolutionsDownscaled){
        QImage image(resolution, resolution, QImage::Format_RGB888);
        image.setColorSpace(QColorSpace::SRgbLinear);
        const float delimeter = image.width() / (cameraSize);
        for (int y = 0; y < image.height(); ++y){
            for (int x = 0; x < image.width(); ++x) {
                const QVector3D origin(
                            cameraOrigin.x(),
                            cameraOrigin.y() - cameraSize * 0.5 + x / delimeter,
                            cameraOrigin.z() - cameraSize * 0.5 + y / delimeter);
                const Color color = cast(shapes, lights, origin, cameraDirection, colorOnMiss, colorOnFullShade);
                image.setPixel(x, y, toRgb(color));
            }
        }
        image = image.scaled(
                    resolutionPrefered,
                    resolutionPrefered,
                    Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
        if (!image.save("output.png")) {
            cout << "can't save output image\n";
            return 1;
        }
    }
    return 0;
}
