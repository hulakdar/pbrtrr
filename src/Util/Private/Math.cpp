#include "Util/Math.hpp"
#include "Util/Debug.h"

Matrix4::Matrix4()
{
	m00 = 1.f;
	m11 = 1.f;
	m22 = 1.f;
	m33 = 1.f;
}

Matrix4::Matrix4(float* Src)
{
	::memcpy(this, Src, sizeof(*this));
}

Vector4 Matrix4::Row(int Index)
{
	CHECK(Index < 4, "");
	Vector4* Ptr = &m0;
	return Ptr[Index];
}

Vector4 Matrix4::Column(int Index)
{
	CHECK(Index < 4, "");
	float* Ptr = &m00;
	Ptr += Index;
	return Vector4{ Ptr[0], Ptr[4], Ptr[8], Ptr[12]};
}

Matrix4& Matrix4::operator*=(Matrix4& Other)
{
	*this = *this * Other;
	return *this;
}

float Dot(Vector2& A, Vector2& B)
{
	return A.x * B.x + A.y * B.y;
}

float Dot(Vector3& A, Vector3& B)
{
	return A.x * B.x + A.y * B.y + A.z * B.z;
}

float Dot(Vector4& A, Vector4& B)
{
	return A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w;
}

Matrix4 operator*(Matrix4& A, Matrix4& B)
{
	float Result[16];
	for (int i = 0; i < 4; ++i)
	for (int j = 0; j < 4; ++j)
	{
		Result[i * 4 + j] = Dot(A.Row(i), B.Column(j));
	}
	return Matrix4(Result);
}

Matrix4 CreatePerspectiveMatrix(float FovInRadians, float AspectRatio, float Near, float Far)
{
	float Scale = 1 / tan(FovInRadians / 2);
	float Result[] = {
		Scale, 0, 0, 0,
		0, Scale, 0, 0,
		0, 0, -Far/(Far-Near), -1,
		0, 0, -Far*Near/(Far-Near), 0,
	};
	return Matrix4(Result);
}

Matrix4 CreateViewMatrix(Vector3 Translation, Vector2 PolarAngles)
{
	float Result[] = {
		1,0,0,0,
		0,1,0,0,
		0,0,1,-1,
		Translation.x, Translation.y, Translation.z, 1,
	};
	return Matrix4(Result);
}

float RadiansToDegrees(float Radians)
{
	return Radians * (180.0f / (float)M_PI);
}

float DegreesToRadians(float Degrees)
{
	return Degrees * ((float)M_PI / 180.0f);
}
