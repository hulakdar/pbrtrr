#include "Util/Math.h"
#include "Util/Debug.h"

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
	::memcpy(&m00, Src, sizeof(*this));
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

Matrix4& Matrix4::operator*=(const Matrix4& Other)
{
	*this = *this * Other;
	return *this;
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

#include <immintrin.h>

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
	__m128 VecResult = _mm_dp_ps(_mm_load_ps(&A.x), _mm_load_ps(&B.x), 255);
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

Vec3 operator-(const Vec3& A, const Vec3& B)
{
	return Vec3{
		A.x - B.x,
		A.y - B.y,
		A.z - B.z
	};
}

Matrix4 CreatePerspectiveMatrixClassic(float FovInRadians, float AspectRatio, float Near, float Far)
{
	float Scale = 1.f / tan(FovInRadians / 2);
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
	float Scale = 1.f / tan(FovInRadians / 2);
	float Result[] = {
		Scale / AspectRatio, 0.0f,  0.0f,  0.0f,
		0.0f,                Scale, 0.0f,  0.0f,
		0.0f,                0.0f,  0.0f, -1.0f,
		0.0f,                0.0f,  Near,  0.0f
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

Matrix4 CreateViewMatrix(Vec3 Translation, Vec2 PolarAngles)
{
	float cosa = cosf(PolarAngles.x);
	float cosb = cosf(PolarAngles.y);
	float cosc = cosf(0);
	float sina = sinf(PolarAngles.x);
	float sinb = sinf(PolarAngles.y);
	float sinc = sinf(0);
	float R[] = {
		cosb*cosc, sina*sinb*cosc-cosa*sinc, cosa*sinb*cosc+sina*sinc, 0,
		cosb*sinc, sina*sinb*sinc+cosa*cosc, cosa*sinb*sinc-sina*cosc, 0,
		   -sinb,  sina*cosb,               -cosa*cosb,                1,
		0,         0,                        0,                        1
	};
	float T[] = {
		1,0,0,0,
		0,1,0,0,
		0,0,1,0,
		Translation.x, Translation.y, Translation.z, 1,
	};
	return Matrix4(T)*Matrix4(R);
}

//Matrix4 CreateViewMatrix(Vec3 Translation, Vec2 YawPitch)
//{
//	float Yaw[] = {
//		cosf(YawPitch.x),  0, sinf(YawPitch.x), 0,
//		0,                 1, 0,                0,
//		-sinf(YawPitch.x), 0, cosf(YawPitch.x), 0,
//		0,                 0, 0,                1,
//	};
//	float Pitch[] = {
//		1, 0,                 0,                0,
//		0, cosf(YawPitch.y), -sinf(YawPitch.y), 0,
//		0, sinf(YawPitch.y),  cosf(YawPitch.y), 0,
//		0, 0,                 0,                1,
//	};
//	float Translate[] = {
//		1, 0, 0, 0,
//		0, 1, 0, 0,
//		0, 0, 1, 0,
//		Translation.x, Translation.y, Translation.z, 1,
//	};
//
//	return Matrix4(Translate) * Matrix4(Yaw) * Matrix4(Pitch);
//}

float RadiansToDegrees(float Radians)
{
	return Radians * (180.0f / (float)M_PI);
}

float DegreesToRadians(float Degrees)
{
	return Degrees * ((float)M_PI / 180.0f);
}

Vec3Pack::Vec3Pack()
{
	x = y = z = 0;
}

Vec3Pack::Vec3Pack(float scalar)
{
	x = y = PackFloat11(scalar);
	z = (x >> 1);
}

Vec3Pack::Vec3Pack(float* p)
{
	x = PackFloat11(p[0]);
	y = PackFloat11(p[1]);
	z = PackFloat10(p[2]);
}

Vec3Pack::Vec3Pack(float xin, float yin, float zin)
{
	x = PackFloat11(xin);
	y = PackFloat11(yin);
	z = PackFloat10(zin);
}

Vec3Pack::operator Vec3()
{
	return Vec3{ UnpackFloat11(x), UnpackFloat11(y), UnpackFloat10(z) };
}

half::half(float x)
{
	Value = PackHalf(x);
}

half::operator float() const
{
	return UnpackHalf(Value);
}

static const uint32_t Max10bit = 0x3ff;
static const uint32_t Max2bit = 0x3;

Vec4PackUnorm::Vec4PackUnorm()
{
	x = y = z = w = 0;
}

Vec4PackUnorm::Vec4PackUnorm(float Xin)
{
	CHECK(Xin >= 0.f && Xin <= 1.f, "This format can only hold normalized values");

	x = y = uint32_t(Xin * Max10bit);
	z = uint32_t(Xin * Max10bit);
	w = 0;
}

Vec4PackUnorm::Vec4PackUnorm(float* p)
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

Vec4PackUnorm::Vec4PackUnorm(float Xin, float Yin, float Zin, float Win)
{
	CHECK(Xin >= 0.f && Xin <= 1.f, "This format can only hold normalized values");
	CHECK(Yin >= 0.f && Yin <= 1.f, "This format can only hold normalized values");
	CHECK(Zin >= 0.f && Zin <= 1.f, "This format can only hold normalized values");

	x = uint32_t(Xin * Max10bit);
	y = uint32_t(Yin * Max10bit);
	z = uint32_t(Zin * Max10bit);
	w = uint32_t(Win * Max2bit);
}

Matrix4Half::Matrix4Half(const Matrix4& Matrix)
{
	half* pThis = &m00;
	const float* pMatrix = &Matrix.m00;
	for (int i = 0; i < 16; ++i)
	{
		*pThis++ = half(*pMatrix++);
	}
}
