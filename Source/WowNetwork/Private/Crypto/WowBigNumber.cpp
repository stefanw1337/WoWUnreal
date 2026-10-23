#include "Crypto/WowBigNumber.h"

#include "OpenSSLIncludes.h"

FWowBigNumber::FWowBigNumber()
{
    BN = BN_new();
}

FWowBigNumber::FWowBigNumber(uint32 Value)
{
    BN = BN_new();
    BN_set_word(BN, Value);
}

FWowBigNumber::FWowBigNumber(const FWowBigNumber& Other)
{
    BN = BN_dup(Other.BN);
}

FWowBigNumber::FWowBigNumber(FWowBigNumber&& Other) noexcept
{
    BN = Other.BN;
    Other.BN = BN_new();
}

FWowBigNumber::~FWowBigNumber()
{
    BN_free(BN);
}

FWowBigNumber& FWowBigNumber::operator=(const FWowBigNumber& Other)
{
    if (this != &Other)
    {
        BN_copy(BN, Other.BN);
    }
    return *this;
}

FWowBigNumber& FWowBigNumber::operator=(FWowBigNumber&& Other) noexcept
{
    if (this != &Other)
    {
        BN_free(BN);
        BN = Other.BN;
        Other.BN = BN_new();
    }
    return *this;
}

void FWowBigNumber::SetBinary(const uint8* Data, int32 Len)
{
    // WoW sends data in little-endian, OpenSSL expects big-endian
    // Reverse the byte order
    TArray<uint8> Reversed;
    Reversed.SetNumUninitialized(Len);
    for (int32 i = 0; i < Len; ++i)
    {
        Reversed[i] = Data[Len - 1 - i];
    }
    BN_bin2bn(Reversed.GetData(), Len, BN);
}

void FWowBigNumber::SetBinary(const TArray<uint8>& Data)
{
    SetBinary(Data.GetData(), Data.Num());
}

void FWowBigNumber::SetRand(int32 NumBits)
{
    BN_rand(BN, NumBits, 0, 1);
}

TArray<uint8> FWowBigNumber::GetBytes(int32 MinSize) const
{
    int32 NumBytes = BN_num_bytes(BN);
    int32 Size = FMath::Max(NumBytes, MinSize);

    // Get big-endian from OpenSSL
    TArray<uint8> BigEndian;
    BigEndian.SetNumZeroed(Size);
    // Write into the end of the buffer so leading zeros are at the start
    BN_bn2bin(BN, BigEndian.GetData() + (Size - NumBytes));

    // Reverse to little-endian for WoW wire format
    TArray<uint8> LittleEndian;
    LittleEndian.SetNumUninitialized(Size);
    for (int32 i = 0; i < Size; ++i)
    {
        LittleEndian[i] = BigEndian[Size - 1 - i];
    }
    return LittleEndian;
}

int32 FWowBigNumber::GetNumBytes() const
{
    return BN_num_bytes(BN);
}

bool FWowBigNumber::IsZero() const
{
    return BN_is_zero(BN) != 0;
}

FWowBigNumber FWowBigNumber::ModExp(const FWowBigNumber& Base, const FWowBigNumber& Exp, const FWowBigNumber& Mod)
{
    FWowBigNumber Result;
    BN_CTX* Ctx = BN_CTX_new();
    BN_mod_exp(Result.BN, Base.BN, Exp.BN, Mod.BN, Ctx);
    BN_CTX_free(Ctx);
    return Result;
}

FWowBigNumber FWowBigNumber::operator+(const FWowBigNumber& Other) const
{
    FWowBigNumber Result;
    BN_add(Result.BN, BN, Other.BN);
    return Result;
}

FWowBigNumber FWowBigNumber::operator-(const FWowBigNumber& Other) const
{
    FWowBigNumber Result;
    BN_sub(Result.BN, BN, Other.BN);
    return Result;
}

FWowBigNumber FWowBigNumber::operator*(const FWowBigNumber& Other) const
{
    FWowBigNumber Result;
    BN_CTX* Ctx = BN_CTX_new();
    BN_mul(Result.BN, BN, Other.BN, Ctx);
    BN_CTX_free(Ctx);
    return Result;
}

FWowBigNumber FWowBigNumber::operator%(const FWowBigNumber& Other) const
{
    FWowBigNumber Result;
    BN_CTX* Ctx = BN_CTX_new();
    BN_mod(Result.BN, BN, Other.BN, Ctx);
    BN_CTX_free(Ctx);
    return Result;
}
