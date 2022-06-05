#pragma once

#include "external/d3dx12.h"

#define _USE_MATH_DEFINES
#include <math.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

template <typename Type>
struct TVec2
{
	Type x;
	Type y;

	TVec2<Type> operator-() { return {-x, -y}; }
};

template <typename Type>
struct TVec3
{
	Type x;
	Type y;
	Type z;

	TVec3<Type> operator-() { return {-x, -y, -z}; }
};

template <typename Type>
struct TVec4
{
	Type x;
	Type y;
	Type z;
	Type w;

	TVec4<Type> operator-() { return {-x, -y, -z, -w}; }
};

struct half
{
	uint16_t Value;

	half() = default;
	half(float x);
	operator float() const;
};

using Vec2 = TVec2<float>;
using Vec3 = TVec3<float>;
using Vec4 = TVec4<float>;

using Vec2h = TVec2<half>;
using Vec3h = TVec3<half>;
using Vec4h = TVec4<half>;

float Dot(const Vec2& A, const Vec2& B);
float Dot(const Vec3& A, const Vec3& B);
float Dot(const Vec4& A, const Vec4& B);

struct Matrix4
{
	Matrix4() = default;
	Matrix4(float*);
	Vec4 Row(int Index);
	Vec4 Column(int Index);
	Matrix4& operator*=(Matrix4& Other);

	float	m00 = 1.f, m01 = 0.f, m02 = 0.f, m03 = 0.f,
			m10 = 0.f, m11 = 1.f, m12 = 0.f, m13 = 0.f,
			m20 = 0.f, m21 = 0.f, m22 = 1.f, m23 = 0.f,
			m30 = 0.f, m31 = 0.f, m32 = 0.f, m33 = 1.f;
};

struct Matrix4Half
{
	Matrix4Half() = default;
	Matrix4Half(const Matrix4&);
	Matrix4Half(float*);

	half	m00 = 1.f, m01 = 0.f, m02 = 0.f, m03 = 0.f,
			m10 = 0.f, m11 = 1.f, m12 = 0.f, m13 = 0.f,
			m20 = 0.f, m21 = 0.f, m22 = 1.f, m23 = 0.f,
			m30 = 0.f, m31 = 0.f, m32 = 0.f, m33 = 1.f;
};

Matrix4 operator*(Matrix4& A, Matrix4&B);

Matrix4 CreateScaleMatrix(Vec3 Scale);
Matrix4 CreateTranslationMatrix(Vec3 Translation);

Matrix4 CreateViewMatrix(Vec3 Translation, Vec2 PolarAngles);
Matrix4 CreatePerspectiveMatrix(float FovInRadians, float AspectRatio, float Near, float Far);

using UVec2 = TVec2<unsigned>;
using IVec2 = TVec2<int>;
using IVec3 = TVec3<int>;
using IVec4 = TVec4<int>;

struct Vec3Pack
{
	uint32_t x : 11;
	uint32_t y : 11;
	uint32_t z : 10;

	Vec3Pack();
	Vec3Pack(float x);
	Vec3Pack(float *p);
	Vec3Pack(float x, float y, float z);
	operator Vec3();
};

struct Vec4PackUnorm
{
	uint32_t x : 10;
	uint32_t y : 10;
	uint32_t z : 10;
	uint32_t w : 2;

	Vec4PackUnorm();
	Vec4PackUnorm(float x);
	Vec4PackUnorm(float *p);
	Vec4PackUnorm(float x, float y, float z, float w = 1.f);
};

float RadiansToDegrees(float Radians);
float DegreesToRadians(float Degrees);

