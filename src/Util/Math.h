#pragma once
#include "Common.h"
#include "Util/Debug.h"

static const uint32_t Max10bit = 0x3ff;
static const uint32_t Max6bit = 0x3f;
static const uint32_t Max5bit = 0x1f;
static const uint32_t Max2bit = 0x3;

template<typename T, typename TT>
T AlignUp(T In, TT AlignmentIn)
{
	T Alignment = (T)AlignmentIn;
	if (In % Alignment != 0)
	{
		In += Alignment - In % Alignment;
	}
	return In;
}

template<typename T, typename TT>
T AlignDown(T In, TT Alignment) { return AlignUp<T>(In + 1 - (T)Alignment, Alignment); }

template<typename T>
T Clamp(T In, T Min, T Max)
{
	In = In < Min ? Min : In;
	In = In > Max ? Max : In;
	return In;
}

template <typename Type>
struct TVec2
{
	Type x;
	Type y;

	TVec2<Type> operator-() { return {-x, -y}; }

	bool operator ==(const TVec2<Type>& Other) {return x == Other.x && y == Other.y;}
};

template <typename Type>
struct TVec3
{
	Type x;
	Type y;
	Type z;

	TVec3() : x(0), y(0), z(0) {}
	TVec3(Type S) : x(S), y(S), z(S) {}
	TVec3(Type *A) : x(A[0]), y(A[1]), z(A[2]) {}
	TVec3(Type X, Type Y, Type Z) : x(X), y(Y), z(Z) {}
	TVec3<Type> operator-() { return {-x, -y, -z}; }
	bool operator==(const TVec3<Type> &Other) { return x == Other.x && y == Other.y && z == Other.z; }
	TVec3<Type> operator +=(const TVec3<Type>& Other) { *this = *this + Other; return *this; }
};

template <typename Type>
struct TVec4
{
	Type x;
	Type y;
	Type z;
	Type w;

	operator TVec3<Type>() { return {x, y, z}; }
	TVec4<Type> operator-() { return {-x, -y, -z, -w}; }
	bool operator==(const TVec4<Type> &Other) { return x == Other.x && y == Other.y && z == Other.z && w == Other.w; }
	TVec4<Type> operator +=(const TVec4<Type>& Other) { *this = *this + Other; return *this; }
};

struct half
{
	uint16_t Value;

	half(float x);
	operator float() const;
};

using UVec2 = TVec2<unsigned>;
using IVec2 = TVec2<int>;
using IVec3 = TVec3<int>;
using IVec4 = TVec4<int>;
using SVec2 = TVec2<short>;

using Vec2 = TVec2<float>;
using Vec3 = TVec3<float>;
using Vec4 = TVec4<float>;

using Vec2d = TVec2<double>;
using Vec3d = TVec3<double>;
using Vec4d = TVec4<double>;

using Vec2h = TVec2<half>;
using Vec3h = TVec3<half>;
using Vec4h = TVec4<half>;

using Vec4b = TVec4<u8>;
using Color4 = Vec4b;

struct LocalBounds
{
	Vec3 BoxExtent;
	float SphereRadius{ 0 };
};

struct Matrix4
{
	Matrix4() = default;
	Matrix4(float*);
	Vec4 Row(int Index) const;
	Vec4 Column(int Index) const;
	Matrix4& operator*=(const Matrix4& Other);

	float	m00 = 1.f, m01 = 0.f, m02 = 0.f, m03 = 0.f,
			m10 = 0.f, m11 = 1.f, m12 = 0.f, m13 = 0.f,
			m20 = 0.f, m21 = 0.f, m22 = 1.f, m23 = 0.f,
			m30 = 0.f, m31 = 0.f, m32 = 0.f, m33 = 1.f;
};

struct Vec4PackShorts
{
	u16 x;
	u16 y;
	u16 z;
	u16 w;

	Vec4PackShorts() { x = y = z = w = 0; }
	Vec4PackShorts(Vec4 In) : Vec4PackShorts(In.x, In.y, In.z, In.w) {}
	Vec4PackShorts(float  In) : Vec4PackShorts(In, In, In, In) {}
	Vec4PackShorts(float *p) : Vec4PackShorts(p[0], p[1], p[2], p[3]) {}

	Vec4PackShorts(float InX, float InY, float InZ, float InW)
	{
		CHECK(InX <= float(UINT16_MAX) && InY <= float(UINT16_MAX) && InZ <= float(UINT16_MAX) && InW <= float(UINT16_MAX));
		x = u16(InX);
		y = u16(InY);
		z = u16(InZ);
		w = u16(InW);
	}

	operator Vec4() { return Vec4{float(x), float(y), float(z), float(w)}; }
};

struct Vec4PackUnorm
{
	union
	{
		u32 Value;
		struct
		{
			u32 x : 10;
			u32 y : 10;
			u32 z : 10;
			u32 w : 2;
		};
	};

	Vec4PackUnorm() { x = y = z = w = 0; }

	Vec4PackUnorm(Vec4 In) : Vec4PackUnorm(In.x, In.y, In.z, In.w) { }

	Vec4PackUnorm(float Xin)
	{
		CHECK(Xin >= 0.f && Xin <= 1.f, "This format can only hold normalized values");

		x = y = z = uint32_t(Xin * Max10bit);
		w = 0;
	}

	Vec4PackUnorm(float* p)
	{
		CHECK(p[0] >= 0.f && p[0] <= 1.f, "This format can only hold normalized values");
		CHECK(p[1] >= 0.f && p[1] <= 1.f, "This format can only hold normalized values");
		CHECK(p[2] >= 0.f && p[2] <= 1.f, "This format can only hold normalized values");
		CHECK(p[3] >= 0.f && p[3] <= 1.f, "This format can only hold normalized values");

		x = uint32_t(p[0] * Max10bit);
		y = uint32_t(p[1] * Max10bit);
		z = uint32_t(p[2] * Max10bit);
		w = uint32_t(p[3] * Max2bit);
	}

	Vec4PackUnorm(float Xin, float Yin, float Zin, float Win)
	{
		CHECK(Xin >= 0.f && Xin <= 1.f, "This format can only hold normalized values");
		CHECK(Yin >= 0.f && Yin <= 1.f, "This format can only hold normalized values");
		CHECK(Zin >= 0.f && Zin <= 1.f, "This format can only hold normalized values");
		CHECK(Win >= 0.f && Win <= 1.f, "This format can only hold normalized values");

		x = uint32_t(Xin * Max10bit);
		y = uint32_t(Yin * Max10bit);
		z = uint32_t(Zin * Max10bit);
		w = uint32_t(Win * Max2bit);
	}

	operator Vec4()
	{
		return Vec4{
			float(x) / Max10bit,
			float(y) / Max10bit,
			float(z) / Max10bit,
			float(w) / Max2bit
		};
	}
};
