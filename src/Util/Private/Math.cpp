#include "Util/Math.h"
#include "Util/Debug.h"

#include <smmintrin.h>
#include <math.h>

namespace {
	uint32_t AsUint(float x) {
		return *(uint32_t*)&x;
	}
	float AsFloat(uint32_t x) {
		return *(float*)&x;
	}

	float UnpackHalf(uint16_t x) { // IEEE-754 16-bit floating-point format (without infinity): 1-5-10, exp-15, +-131008.0, +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
		const uint32_t e = (x&0x7C00)>>10; // exponent
		const uint32_t m = (x&0x03FF)<<13; // mantissa
		const uint32_t v = AsUint((float)m)>>23; // evil log2 bit hack to count leading zeros in denormalized format
		return AsFloat(// sign : normalized : denormalized
			(x&0x8000)<<16 | (e!=0)*((e+112)<<23|m) |
			((e==0)&(m!=0))*((v-37)<<23|((m<<(150-v))&0x7FE000))
		);
	}

	uint16_t PackHalf(float x) { // IEEE-754 16-bit floating-point format (without infinity): 1-5-10, exp-15, +-131008.0, +-6.1035156E-5, +-5.9604645E-8, 3.311 digits
		const uint32_t b = AsUint(x)+0x00001000; // round-to-nearest-even: add last bit after truncated mantissa
		const uint32_t e = (b&0x7F800000)>>23; // exponent
		const uint32_t m = b&0x007FFFFF; // mantissa; in line below: 0x007FF000 = 0x00800000-0x00001000 = decimal indicator flag - initial rounding
		return (uint16_t)(// sign : normalized : denormalized : saturate
			(b&0x80000000)>>16 |
			(e>112)*((((e-112)<<10)&0x7C00)|m>>13) |
			((e<113)&(e>101))*((((0x7FF000+m)>>(125-e))+1)>>1) |
			(e>143)*0x7FFF
		);
	}

	uint16_t PackFloat10(float x)
	{
		CHECK(x >= 0.f, "This format cannot hold negative floats");
		return (PackHalf(x) & 0xefff) >> 5;
	}

	uint16_t PackFloat11(float x)
	{
		CHECK(x >= 0.f, "This format cannot hold negative floats");
		return (PackHalf(x) & 0xefff) >> 4;
	}

	float UnpackFloat10(uint16_t x)
	{
		return UnpackHalf(x << 5);
	}

	float UnpackFloat11(uint16_t x)
	{
		return UnpackHalf(x << 4);
	}
}

Matrix4::Matrix4(float* Src)
{
	_mm_store_ps(&m00, _mm_load_ps(Src));
	_mm_store_ps(&m10, _mm_load_ps(Src + 4));
	_mm_store_ps(&m20, _mm_load_ps(Src + 8));
	_mm_store_ps(&m30, _mm_load_ps(Src + 12));
}

Vec4 Matrix4::Row(int Index) const
{
	CHECK(Index < 4, "");
	Vec4* Ptr = (Vec4*)&m00;
	return Ptr[Index];
}

Vec4 Matrix4::Column(int Index) const
{
	CHECK(Index < 4, "");
	const float* Ptr = &m00;
	Ptr += Index;
	return Vec4{ Ptr[0], Ptr[4], Ptr[8], Ptr[12]};
}

float Lerp(float A, float B, float t)
{
	t = Clamp(t, 0.f, 1.f);
	return A * (1 - t) + B * t;
}

float Dot(const Vec2& A, const Vec2& B)
{
	return A.x * B.x + A.y * B.y;
}

float Dot(const Vec3& A, const Vec3& B)
{
	return A.x * B.x + A.y * B.y + A.z * B.z;
}

#if 1

// linear combination:
// a[0] * B.row[0] + a[1] * B.row[1] + a[2] * B.row[2] + a[3] * B.row[3]
namespace {
	__m128 lincomb_SSE(const __m128 &a, const Matrix4 &B)
	{
		__m128 result;
		result = _mm_mul_ps(_mm_shuffle_ps(a, a, 0x00), _mm_load_ps(&B.m00));
		result = _mm_add_ps(result, _mm_mul_ps(_mm_shuffle_ps(a, a, 0x55), _mm_load_ps(&B.m10)));
		result = _mm_add_ps(result, _mm_mul_ps(_mm_shuffle_ps(a, a, 0xaa), _mm_load_ps(&B.m20)));
		result = _mm_add_ps(result, _mm_mul_ps(_mm_shuffle_ps(a, a, 0xff), _mm_load_ps(&B.m30)));
		return result;
	}

	// this is the right approach for SSE ... SSE4.2
	void matmult_SSE(Matrix4 &out, const Matrix4 &A, const Matrix4 &B)
	{
		// out_ij = sum_k a_ik b_kj
		// => out_0j = a_00 * b_0j + a_01 * b_1j + a_02 * b_2j + a_03 * b_3j
		__m128 out0x = lincomb_SSE(_mm_load_ps(&A.m00), B);
		__m128 out1x = lincomb_SSE(_mm_load_ps(&A.m10), B);
		__m128 out2x = lincomb_SSE(_mm_load_ps(&A.m20), B);
		__m128 out3x = lincomb_SSE(_mm_load_ps(&A.m30), B);

		_mm_store_ps(&out.m00, out0x);
		_mm_store_ps(&out.m10, out1x);
		_mm_store_ps(&out.m20, out2x);
		_mm_store_ps(&out.m30, out3x);
	}
}

Matrix4 operator*(const Matrix4& A, const Matrix4& B)
{
	Matrix4 Result;
	matmult_SSE(Result, A, B);
	return Result;
}

float Dot(const Vec4& A, const Vec4& B)
{
	__m128 VecResult = _mm_dp_ps(_mm_load_ps(&A.x), _mm_load_ps(&B.x), 0xFF);
	float Result[4];
	_mm_store_ps(Result, VecResult);
	return Result[0];
}

#else

Matrix4 operator*(const Matrix4& A, const Matrix4& B)
{
	float Result[16];
	for (int i = 0; i < 4; ++i)
	for (int j = 0; j < 4; ++j)
	{
		Result[i * 4 + j] = Dot(A.Row(i), B.Column(j));
	}
	return Matrix4(Result);
}

float Dot(const Vec4& A, const Vec4& B)
{
	return A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w;
}

#endif

Matrix4& Matrix4::operator*=(const Matrix4& Other)
{
	*this = *this * Other;
	return *this;
}

Vec3 operator*(const Vec3& A, const Matrix4& B)
{
	return Vec3{
		Dot(A, B.Row(0)),
		Dot(A, B.Row(1)),
		Dot(A, B.Row(2))
	};
}

Vec4 operator*(const Vec4& A, const Matrix4& B)
{
	return Vec4{
		Dot(A, B.Row(0)),
		Dot(A, B.Row(1)),
		Dot(A, B.Row(2))
	};
}

float DistanceSquared(const Vec4& A, const Vec4& B)
{
	Vec4 Result{A.x - B.x, A.y - B.y, A.z - B.z, A.w - B.w};
	return Dot(Result, Result);
}

Vec2 operator-(const Vec2& A, const Vec2& B)
{
	return Vec2{
		A.x - B.x,
		A.y - B.y
	};
}

Vec2 operator+(const Vec2& A, const Vec2& B)
{
	return Vec2{
		A.x + B.x,
		A.y + B.y
	};
}

Vec3 operator-(const Vec3& A, const Vec3& B)
{
	return Vec3{
		A.x - B.x,
		A.y - B.y,
		A.z - B.z
	};
}

Vec3 operator+(const Vec3& A, const Vec3& B)
{
	return Vec3{
		A.x + B.x,
		A.y + B.y,
		A.z + B.z
	};
}

Matrix4 CreatePerspectiveMatrixClassic(float FovInRadians, float AspectRatio, float Near, float Far)
{
	float Scale = 1.f / tanf(FovInRadians / 2);
	float Extent = Far-Near;
	float Result[] = {
		Scale/AspectRatio, 0,     0,               0,
		0,                 Scale, 0,               0,
		0,                 0,    -Far/Extent,     -1,
		0,                 0,    -Far*Near/Extent, 0,
	};
	return Matrix4(Result);
}

Matrix4 CreatePerspectiveMatrixReverseZ(float FovInRadians, float AspectRatio, float Near)
{
	float Scale = 1.f / tanf(FovInRadians / 2);
	float Result[] = {
		Scale / AspectRatio, 0.0f,  0.0f,  0.0f,
		0.0f,                Scale, 0.0f,  0.0f,
		0.0f,                0.0f,  0.0f,  Near,
		0.0f,                0.0f,  1.0f,  0.0f
	};
	return Matrix4(Result);
}

Matrix4 CreateTranslationMatrix(Vec3 Translation)
{
	float Result[] = {
		1,             0,             0,             0,
		0,             1,             0,             0,
		0,             0,             1,             0,
		Translation.x, Translation.y, Translation.z, 1,
	};
	return Matrix4(Result);
}

Matrix4 InverseAffine(const Matrix4& M)
{
	float Result[] = {
		 M.m00,  M.m10,  M.m20, 0,
		 M.m01,  M.m11,  M.m21, 0,
		 M.m02,  M.m12,  M.m22, 0,
		-M.m30, -M.m31, -M.m32, 1,
	};
	return Matrix4(Result);
}

Matrix4 CreateScaleMatrix(Vec3 Scale)
{
	float Result[] = {
		Scale.x, 0,       0,       0,
		0,       Scale.y, 0,       0,
		0,       0,       Scale.z, 0,
		0,       0,       0,       1,
	};
	return Matrix4(Result);
}

Matrix4 CreateRotationMatrix(Vec2 YawPitch)
{
	float Yaw[] = {
		cosf(YawPitch.x),  0, sinf(YawPitch.x), 0,
		0,                 1, 0,                0,
		-sinf(YawPitch.x), 0, cosf(YawPitch.x), 0,
		0,                 0, 0,                1,
	};
	float Pitch[] = {
		1, 0,                 0,                0,
		0, cosf(YawPitch.y), -sinf(YawPitch.y), 0,
		0, sinf(YawPitch.y),  cosf(YawPitch.y), 0,
		0, 0,                 0,                1,
	};

	return Matrix4(Yaw) * Matrix4(Pitch);
}

#if 0 
Matrix4 CreateViewMatrix(Vec3 Translation, Vec2 PolarAngles)
{
	float cosa = cosf(PolarAngles.x);
	float cosb = cosf(PolarAngles.y);
	float cosc = cosf(0);
	float sina = sinf(PolarAngles.x);
	float sinb = sinf(PolarAngles.y);
	float sinc = sinf(0);
	float Result[] = {
		cosb*cosc,                cosb*sinc,                -sinb,          0,

		sina*sinb*cosc-cosa*sinc, sina*sinb*sinc+cosa*cosc,  sina*cosb,     0,

		cosa*sinb*cosc+sina*sinc, cosa*sinb*sinc-sina*cosc, -cosa*cosb,     0,

		-Translation.x,          -Translation.y,            -Translation.z, 1
	};
	return Matrix4(Result);
}
#else
Matrix4 CreateViewMatrix(Vec3 Translation, Vec2 YawPitch)
{
	float Yaw[] = {
		cosf(-YawPitch.x),  0, sinf(-YawPitch.x), 0,
		0,                 1, 0,                0,
		-sinf(-YawPitch.x), 0, cosf(-YawPitch.x), 0,
		0,                 0, 0,                1,
	};
	float Pitch[] = {
		1, 0,                 0,                0,
		0, cosf(-YawPitch.y), -sinf(-YawPitch.y), 0,
		0, sinf(-YawPitch.y),  cosf(-YawPitch.y), 0,
		0, 0,                 0,                1,
	};
	float Translate[] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		-Translation.x, -Translation.y, -Translation.z, 1,
	};

	return Matrix4(Translate) * Matrix4(Yaw) * Matrix4(Pitch);
}
#endif

half::half(float x)
{
	Value = PackHalf(x);
}

half::operator float() const
{
	return UnpackHalf(Value);
}

u16 RGBto565(float r, float g, float b)
{
	u16 Result = u16(Clamp<float>(r * (float)Max5bit, 0, Max5bit)) & Max5bit;
	Result |= (u16(Clamp<float>(g * (float)Max5bit, 0, Max6bit)) & Max6bit) << 5;
	Result |= (u16(Clamp<float>(b * (float)Max5bit, 0, Max5bit)) & Max5bit) << 11;
	return Result;
}