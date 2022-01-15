#pragma once

#include "external/d3dx12.h"

#define _USE_MATH_DEFINES
#include <math.h>

#define MIN(a,b) ((a) < (b) ? (a : b));
#define MAX(a,b) ((a) > (b) ? (a : b));

template <typename Type>
struct TVector2
{
	Type x;
	Type y;

	TVector2<Type> operator-() { return {-x, -y}; }
};

template <typename Type>
struct TVector3
{
	Type x;
	Type y;
	Type z;

	TVector3<Type> operator-() { return {-x, -y, -z}; }
};

template <typename Type>
struct TVector4
{
	Type x;
	Type y;
	Type z;
	Type w;

	TVector4<Type> operator-() { return {-x, -y, -z, -w}; }
};

using Vector2 = TVector2<float>;
using Vector3 = TVector3<float>;
using Vector4 = TVector4<float>;

float Dot(Vector2& A, Vector2& B);
float Dot(Vector3& A, Vector3& B);
float Dot(Vector4& A, Vector4& B);

struct Matrix4
{
	Matrix4();
	Matrix4(float*);
	union {
		struct {
			Vector4 m0;
			Vector4 m1;
			Vector4 m2;
			Vector4 m3;
		};
		struct {
			float	m00, m01, m02, m03,
					m10, m11, m12, m13,
					m20, m21, m22, m23,
					m30, m31, m32, m33;
		};
	};
	Vector4 Row(int Index);
	Vector4 Column(int Index);
	Matrix4& operator*=(Matrix4& Other);
};

Matrix4 operator*(Matrix4& A, Matrix4&B);

Matrix4 CreatePerspectiveMatrix(float FovInRadians, float AspectRatio, float Near, float Far);
Matrix4 CreateViewMatrix(Vector3 Translation, Vector2 PolarAngles);

using IVector2 = TVector2<int>;
using IVector3 = TVector3<int>;
using IVector4 = TVector4<int>;

float RadiansToDegrees(float Radians);
float DegreesToRadians(float Degrees);
